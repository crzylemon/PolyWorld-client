/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: ios_boot.m                                                                          |
|   Purpose: UIKit entry: EAGL view + the real game loop                                      |
\*-------------------------------------------------------------------------------------------*/

#ifdef PW_IOS

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <OpenGLES/EAGL.h>

#include "platform.h"
#include "platform_ios.h"
#include "input.h"
#include "pw_android_game.h"

@interface PWGLView : UIView
@end

@implementation PWGLView
+ (Class)layerClass {
    return [CAEAGLLayer class];
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)event;
    UITouch* t = touches.anyObject;
    CGPoint p = [t locationInView:self];
    platform_ios_on_touch((float)p.x, (float)p.y, 0);
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)event;
    UITouch* t = touches.anyObject;
    CGPoint p = [t locationInView:self];
    platform_ios_on_touch((float)p.x, (float)p.y, 1);
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)event;
    UITouch* t = touches.anyObject;
    CGPoint p = [t locationInView:self];
    platform_ios_on_touch((float)p.x, (float)p.y, 2);
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self touchesEnded:touches withEvent:event];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    platform_ios_on_resize();
}
@end

@interface PWAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow* window;
@property (nonatomic, strong) CADisplayLink* link;
@end

@implementation PWAppDelegate

- (void)onFrame:(CADisplayLink*)link {
    (void)link;
    platform_ios_tick();
    platform_flush_frame();
}

- (BOOL)application:(UIApplication*)app didFinishLaunchingWithOptions:(NSDictionary*)opts {
    (void)app; (void)opts;
    CGRect bounds = UIScreen.mainScreen.bounds;
    self.window = [[UIWindow alloc] initWithFrame:bounds];
    UIViewController* vc = [UIViewController new];
    PWGLView* gl = [[PWGLView alloc] initWithFrame:bounds];
    gl.multipleTouchEnabled = NO;
    vc.view = gl;
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];

    platform_ios_attach(gl);
    input_init();
    if (!platform_init(0, 0, "PolyWorld")) {
        NSLog(@"[PolyWorld] platform_init failed");
        return NO;
    }
    if (!pw_game_init()) {
        NSLog(@"[PolyWorld] pw_game_init failed");
        return NO;
    }
    platform_run_loop(pw_game_frame);

    self.link = [CADisplayLink displayLinkWithTarget:self selector:@selector(onFrame:)];
    self.link.preferredFramesPerSecond = 60;
    [self.link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    return YES;
}

- (void)applicationWillResignActive:(UIApplication*)app {
    (void)app;
    self.link.paused = YES;
}

- (void)applicationDidBecomeActive:(UIApplication*)app {
    (void)app;
    self.link.paused = NO;
}

@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([PWAppDelegate class]));
    }
}

#endif /* PW_IOS */
