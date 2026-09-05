#ifndef PLATFORM_IOS_H
#define PLATFORM_IOS_H

#ifdef __OBJC__
@class UIView;
void platform_ios_attach(UIView* view);
#endif

#ifdef __cplusplus
extern "C" {
#endif

void platform_ios_tick(void);
void platform_ios_on_touch(float x, float y, int phase);
void platform_ios_on_resize(void);

#ifdef __cplusplus
}
#endif

#endif
