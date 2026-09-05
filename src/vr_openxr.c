/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: vr_openxr.c                                                                         |
|   Purpose: desktop OpenXR (Monado)                                                          |
\*-------------------------------------------------------------------------------------------*/

#if defined(VR) && defined(PW_OPENXR) && !defined(PW_QUEST)

#include "vr_openxr.h"
#include "log.h"
#include <openxr/openxr.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define XR_CHK(expr) do { \
    XrResult _r = (expr); \
    if (XR_FAILED(_r)) { \
        PW_LOG("[VR] OpenXR %s failed (%d)\n", #expr, (int)_r); \
        return false; \
    } \
} while (0)

typedef struct {
    XrInstance instance;
    XrSystemId system;
    XrSession session;
    XrSpace local;
    XrActionSet action_set;
    XrAction pose_l, pose_r;
    XrSpace hand_l, hand_r;
    bool session_running;
    Vec3 origin_feet;
} Oxr;

static Oxr g_oxr;

static bool has_ext(const char* name) {
    uint32_t n = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL)) || n == 0)
        return false;
    XrExtensionProperties* props = (XrExtensionProperties*)calloc(n, sizeof(*props));
    if (!props) return false;
    for (uint32_t i = 0; i < n; i++)
        props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    uint32_t got = n;
    bool ok = false;
    if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(NULL, n, &got, props))) {
        for (uint32_t i = 0; i < got; i++) {
            if (strcmp(props[i].extensionName, name) == 0) {
                ok = true;
                break;
            }
        }
    }
    free(props);
    return ok;
}

static void quat_to_yp(XrQuaternionf q, float* yaw_deg, float* pitch_deg) {

    float x = q.x, y = q.y, z = q.z, w = q.w;
    Vec3 f = {
        -(2.0f * (x * z + w * y)),
        -(2.0f * (y * z - w * x)),
        -(1.0f - 2.0f * (x * x + y * y))
    };
    float len = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
    if (len > 1e-6f) {
        f.x /= len; f.y /= len; f.z /= len;
    }
    if (yaw_deg)
        *yaw_deg = atan2f(-f.x, -f.z) * (180.0f / (float)M_PI);
    if (pitch_deg) {
        float fy = f.y < -1.f ? -1.f : (f.y > 1.f ? 1.f : f.y);
        *pitch_deg = -asinf(fy) * (180.0f / (float)M_PI);
    }
}

static void tracker_from_pose(const XrPosef* xp, PwVrTracker* t, Vec3 origin) {
    t->x = origin.x + pw_metres_to_studs(xp->position.x);
    t->y = origin.y + pw_metres_to_studs(xp->position.y);
    t->z = origin.z + pw_metres_to_studs(xp->position.z);
    t->qx = xp->orientation.x;
    t->qy = xp->orientation.y;
    t->qz = xp->orientation.z;
    t->qw = xp->orientation.w;
}

bool vr_openxr_init(void) {
    memset(&g_oxr, 0, sizeof(g_oxr));

    const char* headless = XR_MND_HEADLESS_EXTENSION_NAME;
    bool use_headless = has_ext(headless);
    if (!use_headless)
        PW_WARN("[VR] OpenXR runtime has no XR_MND_headless. need Monado for pose-only\n");

    XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
    strncpy(ci.applicationInfo.applicationName, "PolyWorld", XR_MAX_APPLICATION_NAME_SIZE - 1);

#ifdef XR_API_VERSION_1_0
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
#else
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
#endif
    if (use_headless) {
        ci.enabledExtensionCount = 1;
        ci.enabledExtensionNames = &headless;
    }

    XrResult r = xrCreateInstance(&ci, &g_oxr.instance);
    if (XR_FAILED(r)) {
        PW_WARN("[VR] xrCreateInstance failed (%d). Is monado-service running?\n", (int)r);
        return false;
    }

    XrSystemGetInfo si = { XR_TYPE_SYSTEM_GET_INFO };
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (XR_FAILED(xrGetSystem(g_oxr.instance, &si, &g_oxr.system))) {
        PW_WARN("[VR] xrGetSystem failed (no HMD. start Monado simulated devices)\n");
        xrDestroyInstance(g_oxr.instance);
        g_oxr.instance = 0;
        return false;
    }

    XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
    sci.systemId = g_oxr.system;
    if (XR_FAILED(xrCreateSession(g_oxr.instance, &sci, &g_oxr.session))) {
        PW_WARN("[VR] xrCreateSession failed (need XR_MND_headless for pose-only)\n");
        xrDestroyInstance(g_oxr.instance);
        g_oxr.instance = 0;
        return false;
    }

    XrReferenceSpaceCreateInfo rs = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rs.poseInReferenceSpace.orientation.w = 1.0f;
    if (XR_FAILED(xrCreateReferenceSpace(g_oxr.session, &rs, &g_oxr.local))) {
        xrDestroySession(g_oxr.session);
        xrDestroyInstance(g_oxr.instance);
        memset(&g_oxr, 0, sizeof(g_oxr));
        return false;
    }

    XrActionSetCreateInfo asci = { XR_TYPE_ACTION_SET_CREATE_INFO };
    strncpy(asci.actionSetName, "pw_vr", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    strncpy(asci.localizedActionSetName, "PolyWorld VR", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    if (XR_SUCCEEDED(xrCreateActionSet(g_oxr.instance, &asci, &g_oxr.action_set))) {
        XrActionCreateInfo aci = { XR_TYPE_ACTION_CREATE_INFO };
        aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
        strncpy(aci.actionName, "lhand", XR_MAX_ACTION_NAME_SIZE - 1);
        strncpy(aci.localizedActionName, "Left hand", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        xrCreateAction(g_oxr.action_set, &aci, &g_oxr.pose_l);
        strncpy(aci.actionName, "rhand", XR_MAX_ACTION_NAME_SIZE - 1);
        strncpy(aci.localizedActionName, "Right hand", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        xrCreateAction(g_oxr.action_set, &aci, &g_oxr.pose_r);

        XrPath profile = 0, p_lg = 0, p_rg = 0;
        xrStringToPath(g_oxr.instance, "/interaction_profiles/khr/simple_controller", &profile);
        xrStringToPath(g_oxr.instance, "/user/hand/left/input/grip/pose", &p_lg);
        xrStringToPath(g_oxr.instance, "/user/hand/right/input/grip/pose", &p_rg);
        XrActionSuggestedBinding binds[2];
        binds[0].action = g_oxr.pose_l;
        binds[0].binding = p_lg;
        binds[1].action = g_oxr.pose_r;
        binds[1].binding = p_rg;
        XrInteractionProfileSuggestedBinding sug = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        sug.interactionProfile = profile;
        sug.countSuggestedBindings = 2;
        sug.suggestedBindings = binds;
        xrSuggestInteractionProfileBindings(g_oxr.instance, &sug);

        XrSessionActionSetsAttachInfo att = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
        att.countActionSets = 1;
        att.actionSets = &g_oxr.action_set;
        xrAttachSessionActionSets(g_oxr.session, &att);

        XrActionSpaceCreateInfo asci_s = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
        asci_s.poseInActionSpace.orientation.w = 1.0f;
        asci_s.action = g_oxr.pose_l;
        xrCreateActionSpace(g_oxr.session, &asci_s, &g_oxr.hand_l);
        asci_s.action = g_oxr.pose_r;
        xrCreateActionSpace(g_oxr.session, &asci_s, &g_oxr.hand_r);
    }

    {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        bool ready = false;
        while (!ready) {
            XrEventDataBuffer ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = XR_TYPE_EVENT_DATA_BUFFER;
            XrResult er = xrPollEvent(g_oxr.instance, &ev);
            if (er == XR_SUCCESS && ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                XrEventDataSessionStateChanged* sc = (XrEventDataSessionStateChanged*)&ev;
                if (sc->state == XR_SESSION_STATE_READY)
                    ready = true;
                else if (sc->state == XR_SESSION_STATE_EXITING ||
                         sc->state == XR_SESSION_STATE_LOSS_PENDING) {
                    PW_WARN("[VR] OpenXR session lost before READY\n");
                    vr_openxr_shutdown();
                    return false;
                }
            } else if (er != XR_SUCCESS && er != XR_EVENT_UNAVAILABLE) {
                break;
            }
            if (!ready) {
                clock_gettime(CLOCK_MONOTONIC, &t1);
                long ms = (long)((t1.tv_sec - t0.tv_sec) * 1000 +
                                 (t1.tv_nsec - t0.tv_nsec) / 1000000);
                if (ms > 2000) {
                    PW_WARN("[VR] Timed out waiting for OpenXR SESSION_READY\n");
                    vr_openxr_shutdown();
                    return false;
                }
                struct timespec sl = { 0, 10 * 1000 * 1000 };
                nanosleep(&sl, NULL);
            }
        }
    }

    XrSessionBeginInfo bi = { XR_TYPE_SESSION_BEGIN_INFO };
    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    if (XR_FAILED(xrBeginSession(g_oxr.session, &bi))) {
        PW_WARN("[VR] xrBeginSession failed\n");
        vr_openxr_shutdown();
        return false;
    }
    g_oxr.session_running = true;
    PW_WARN("[VR] OpenXR session started (Monado/headless poses)\n");
    return true;
}

void vr_openxr_shutdown(void) {
    if (g_oxr.session_running && g_oxr.session) {
        xrRequestExitSession(g_oxr.session);
        xrEndSession(g_oxr.session);
    }
    if (g_oxr.hand_l) xrDestroySpace(g_oxr.hand_l);
    if (g_oxr.hand_r) xrDestroySpace(g_oxr.hand_r);
    if (g_oxr.pose_l) xrDestroyAction(g_oxr.pose_l);
    if (g_oxr.pose_r) xrDestroyAction(g_oxr.pose_r);
    if (g_oxr.action_set) xrDestroyActionSet(g_oxr.action_set);
    if (g_oxr.local) xrDestroySpace(g_oxr.local);
    if (g_oxr.session) xrDestroySession(g_oxr.session);
    if (g_oxr.instance) xrDestroyInstance(g_oxr.instance);
    memset(&g_oxr, 0, sizeof(g_oxr));
}

bool vr_openxr_poll(PwVrPose* pose, float* yaw_deg, float* pitch_deg,
                    Vec3* eye_studs, Vec3 origin_feet) {
    if (!g_oxr.session_running || !pose) return false;

    XrFrameWaitInfo wi = { XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState fs = { XR_TYPE_FRAME_STATE };
    if (XR_FAILED(xrWaitFrame(g_oxr.session, &wi, &fs)))
        return false;

    XrFrameBeginInfo bi = { XR_TYPE_FRAME_BEGIN_INFO };
    xrBeginFrame(g_oxr.session, &bi);

    memset(pose, 0, sizeof(*pose));
    pose->head.qw = pose->lhand.qw = pose->rhand.qw = 1.0f;
    pose->flags = PW_VR_FLAG_ACTIVE;

    g_oxr.origin_feet = origin_feet;
    Vec3 origin = origin_feet;

    uint32_t nviews = 0;
    xrEnumerateViewConfigurationViews(g_oxr.instance, g_oxr.system,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nviews, NULL);
    XrView views[2];
    views[0].type = views[1].type = XR_TYPE_VIEW;
    views[0].next = views[1].next = NULL;
    XrViewLocateInfo vli = { XR_TYPE_VIEW_LOCATE_INFO };
    vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    vli.displayTime = fs.predictedDisplayTime;
    vli.space = g_oxr.local;
    XrViewState vs = { XR_TYPE_VIEW_STATE };
    uint32_t got = 0;
    if (nviews > 2) nviews = 2;
    if (nviews > 0 &&
        XR_SUCCEEDED(xrLocateViews(g_oxr.session, &vli, &vs, nviews, &got, views)) &&
        got > 0 && (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)) {

        XrPosef hp = views[0].pose;
        if (got > 1) {
            hp.position.x = 0.5f * (views[0].pose.position.x + views[1].pose.position.x);
            hp.position.y = 0.5f * (views[0].pose.position.y + views[1].pose.position.y);
            hp.position.z = 0.5f * (views[0].pose.position.z + views[1].pose.position.z);
        }
        tracker_from_pose(&hp, &pose->head, origin);
        pose->flags |= PW_VR_FLAG_HEAD;
        quat_to_yp(hp.orientation, yaw_deg, pitch_deg);
        if (eye_studs) {
            eye_studs->x = pose->head.x;
            eye_studs->y = pose->head.y;
            eye_studs->z = pose->head.z;
        }
    }

    if (g_oxr.action_set) {
        XrActiveActionSet aas = { g_oxr.action_set, XR_NULL_PATH };
        XrActionsSyncInfo syn = { XR_TYPE_ACTIONS_SYNC_INFO };
        syn.countActiveActionSets = 1;
        syn.activeActionSets = &aas;
        xrSyncActions(g_oxr.session, &syn);

        XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
        if (g_oxr.hand_l &&
            XR_SUCCEEDED(xrLocateSpace(g_oxr.hand_l, g_oxr.local, fs.predictedDisplayTime, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
            tracker_from_pose(&loc.pose, &pose->lhand, origin);
            pose->flags |= PW_VR_FLAG_LHAND;
        }
        loc = (XrSpaceLocation){ XR_TYPE_SPACE_LOCATION };
        if (g_oxr.hand_r &&
            XR_SUCCEEDED(xrLocateSpace(g_oxr.hand_r, g_oxr.local, fs.predictedDisplayTime, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
            tracker_from_pose(&loc.pose, &pose->rhand, origin);
            pose->flags |= PW_VR_FLAG_RHAND;
        }
    }

    XrFrameEndInfo ei = { XR_TYPE_FRAME_END_INFO };
    ei.displayTime = fs.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = 0;
    xrEndFrame(g_oxr.session, &ei);
    return (pose->flags & PW_VR_FLAG_HEAD) != 0;
}

void vr_openxr_snap_playspace(void) {}
bool vr_openxr_consume_recalibrate(void) { return false; }
void vr_openxr_set_comfort(int turn_mode, bool ui_cursor) { (void)turn_mode; (void)ui_cursor; }
float vr_openxr_turn_amount(void) { return 0.0f; }
bool vr_openxr_consume_pause(void) { return false; }

#endif

#if !defined(VR) || !defined(PW_OPENXR) || defined(PW_QUEST)
typedef int pw_vr_openxr_desktop_tu;
#endif
