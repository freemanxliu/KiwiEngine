#include "Core/Window.h"

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cmath>
#include <stdexcept>

@interface KiwiMetalView : NSView
@end

@implementation KiwiMetalView
- (CALayer*)makeBackingLayer
{
    return [CAMetalLayer layer];
}
- (BOOL)wantsUpdateLayer { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
@end

@interface KiwiWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) Kiwi::Window* owner;
@end

@implementation KiwiWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)window
{
    (void)window;
    if (_owner)
        _owner->NotifyClose();
    return NO;
}

- (void)windowDidResize:(NSNotification*)notification
{
    if (!_owner)
        return;
    NSWindow* window = notification.object;
    NSView* view = window.contentView;
    CGFloat scale = window.backingScaleFactor > 0 ? window.backingScaleFactor : 1.0;
    auto width = (uint32_t)std::llround(view.bounds.size.width * scale);
    auto height = (uint32_t)std::llround(view.bounds.size.height * scale);
    if ([view.layer isKindOfClass:[CAMetalLayer class]])
    {
        CAMetalLayer* layer = (CAMetalLayer*)view.layer;
        layer.contentsScale = scale;
        layer.drawableSize = CGSizeMake(width, height);
    }
    _owner->NotifyResize(width, height);
}
@end

namespace Kiwi
{
    namespace
    {
        void EnsureNSApp()
        {
            if (NSApp)
                return;
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        }

        uint8_t MapKey(NSEvent* event)
        {
            switch (event.keyCode)
            {
            case 126: return VK_UP;
            case 125: return VK_DOWN;
            case 123: return VK_LEFT;
            case 124: return VK_RIGHT;
            case 53:  return VK_ESCAPE;
            default:  break;
            }

            NSString* chars = event.charactersIgnoringModifiers;
            if (chars.length == 0)
                return 0;
            unichar c = [chars characterAtIndex:0];
            if (c >= 'a' && c <= 'z')
                c = (unichar)(c - 'a' + 'A');
            return c < 256 ? (uint8_t)c : 0;
        }
    }

    Window::Window(const WindowDesc& desc)
        : m_Width(desc.Width)
        , m_Height(desc.Height)
    {
        @autoreleasepool
        {
            EnsureNSApp();

            NSRect content = NSMakeRect(0, 0, desc.Width, desc.Height);
            NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
            NSWindow* window = [[NSWindow alloc] initWithContentRect:content
                styleMask:style backing:NSBackingStoreBuffered defer:NO];
            window.title = [NSString stringWithUTF8String:(desc.Title ? desc.Title : "Kiwi Engine")];
            [window center];
            window.acceptsMouseMovedEvents = YES;

            KiwiMetalView* view = [[KiwiMetalView alloc] initWithFrame:content];
            view.wantsLayer = YES;
            window.contentView = view;

            CAMetalLayer* layer = (CAMetalLayer*)view.layer;
            layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            layer.framebufferOnly = YES;
            layer.opaque = YES;

            CGFloat scale = window.screen.backingScaleFactor;
            if (scale <= 0)
                scale = NSScreen.mainScreen.backingScaleFactor;
            if (scale <= 0)
                scale = 1.0;
            m_Width = (uint32_t)std::llround(desc.Width * scale);
            m_Height = (uint32_t)std::llround(desc.Height * scale);
            layer.contentsScale = scale;
            layer.drawableSize = CGSizeMake(m_Width, m_Height);

            auto* delegate = [KiwiWindowDelegate new];
            delegate.owner = this;
            window.delegate = delegate;

            m_Window = (__bridge_retained void*)window;
            m_View = (__bridge void*)view;
            m_Delegate = (__bridge_retained void*)delegate;
        }
    }

    Window::~Window()
    {
        @autoreleasepool
        {
            NSWindow* window = (__bridge_transfer NSWindow*)m_Window;
            window.delegate = nil;
            [window orderOut:nil];
            window = nil;
            m_Window = nullptr;
            m_View = nullptr;

            KiwiWindowDelegate* delegate = (__bridge_transfer KiwiWindowDelegate*)m_Delegate;
            delegate = nil;
            m_Delegate = nullptr;
        }
    }

    void Window::Show()
    {
        @autoreleasepool
        {
            NSWindow* window = (__bridge NSWindow*)m_Window;
            [window makeKeyAndOrderFront:nil];
            [window makeFirstResponder:window.contentView];
            [NSApp activate];
        }
    }

    void Window::Hide()
    {
        NSWindow* window = (__bridge NSWindow*)m_Window;
        [window orderOut:nil];
    }

    void Window::SetTitle(const std::string& title)
    {
        NSWindow* window = (__bridge NSWindow*)m_Window;
        window.title = [NSString stringWithUTF8String:title.c_str()];
    }

    void Window::PumpMessages()
    {
        @autoreleasepool
        {
            ResetFrameState();
            NSWindow* window = (__bridge NSWindow*)m_Window;
            NSView* view = (__bridge NSView*)m_View;

            while (NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                untilDate:[NSDate distantPast]
                inMode:NSDefaultRunLoopMode
                dequeue:YES])
            {
                if (event.window == window)
                {
                    switch (event.type)
                    {
                    case NSEventTypeMouseMoved:
                    case NSEventTypeLeftMouseDragged:
                    case NSEventTypeRightMouseDragged:
                    case NSEventTypeOtherMouseDragged:
                    case NSEventTypeLeftMouseDown:
                    case NSEventTypeLeftMouseUp:
                    case NSEventTypeRightMouseDown:
                    case NSEventTypeRightMouseUp:
                    {
                        NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
                        CGFloat scale = window.backingScaleFactor > 0 ? window.backingScaleFactor : 1.0;
                        auto x = (int32_t)std::llround(point.x * scale);
                        auto y = (int32_t)std::llround((view.bounds.size.height - point.y) * scale);
                        NotifyMouseMove(x, y);
                        if (event.type == NSEventTypeLeftMouseDown)
                            NotifyMouseButton(0, true);
                        else if (event.type == NSEventTypeLeftMouseUp)
                            NotifyMouseButton(0, false);
                        else if (event.type == NSEventTypeRightMouseDown)
                            NotifyMouseButton(1, true);
                        else if (event.type == NSEventTypeRightMouseUp)
                            NotifyMouseButton(1, false);
                        break;
                    }
                    case NSEventTypeKeyDown:
                        if (event.keyCode == 53)
                            NotifyClose();
                        NotifyKey(MapKey(event), true);
                        break;
                    case NSEventTypeKeyUp:
                        NotifyKey(MapKey(event), false);
                        break;
                    default:
                        break;
                    }
                }
                [NSApp sendEvent:event];
            }
        }
    }

}
