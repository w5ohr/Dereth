// Renders the app icon into an .iconset. Run through tools/make_icon.sh, which
// hands the result to iconutil. Kept as source so the icon can be re-cut
// whenever the palette changes.
#import <AppKit/AppKit.h>

#include <string>
#include <vector>

namespace {

// The app's accent blue over the window background, so the icon reads as part
// of the same set as the UI.
const CGFloat kTopR = 0.22, kTopG = 0.28, kTopB = 0.42;
const CGFloat kBottomR = 0.05, kBottomG = 0.06, kBottomB = 0.09;
const CGFloat kAccentR = 0.42, kAccentG = 0.60, kAccentB = 1.0;

void drawIcon(CGContextRef ctx, CGFloat size) {
  const CGFloat inset = size * 0.055;
  const CGRect body = CGRectMake(inset, inset, size - inset * 2, size - inset * 2);
  const CGFloat radius = body.size.width * 0.2237;  // macOS squircle approximation

  // Rounded background with a top-down gradient.
  CGPathRef rounded = CGPathCreateWithRoundedRect(body, radius, radius, nullptr);
  CGContextSaveGState(ctx);
  CGContextAddPath(ctx, rounded);
  CGContextClip(ctx);

  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  const CGFloat components[] = {kTopR, kTopG, kTopB, 1.0, kBottomR, kBottomG, kBottomB, 1.0};
  const CGFloat locations[] = {0.0, 1.0};
  CGGradientRef gradient = CGGradientCreateWithColorComponents(space, components, locations, 2);
  CGContextDrawLinearGradient(ctx, gradient,
                              CGPointMake(0, CGRectGetMaxY(body)),
                              CGPointMake(0, CGRectGetMinY(body)), 0);
  CGGradientRelease(gradient);

  // A soft highlight across the top edge.
  const CGFloat glossComponents[] = {1, 1, 1, 0.10, 1, 1, 1, 0.0};
  CGGradientRef gloss = CGGradientCreateWithColorComponents(space, glossComponents, locations, 2);
  CGContextDrawLinearGradient(ctx, gloss, CGPointMake(0, CGRectGetMaxY(body)),
                              CGPointMake(0, CGRectGetMidY(body)), 0);
  CGGradientRelease(gloss);
  CGContextRestoreGState(ctx);

  // Hairline edge.
  CGContextSetRGBStrokeColor(ctx, 1, 1, 1, 0.12);
  CGContextSetLineWidth(ctx, size * 0.006);
  CGContextAddPath(ctx, rounded);
  CGContextStrokePath(ctx);
  CGPathRelease(rounded);

  // Speech bubble.
  const CGFloat bubbleW = size * 0.50;
  const CGFloat bubbleH = size * 0.37;
  const CGRect bubble = CGRectMake((size - bubbleW) * 0.5, size * 0.355, bubbleW, bubbleH);
  const CGFloat bubbleRadius = bubbleH * 0.30;
  CGPathRef bubblePath =
      CGPathCreateWithRoundedRect(bubble, bubbleRadius, bubbleRadius, nullptr);
  CGContextSetRGBStrokeColor(ctx, kAccentR, kAccentG, kAccentB, 1.0);
  CGContextSetLineWidth(ctx, size * 0.042);
  CGContextSetLineJoin(ctx, kCGLineJoinRound);
  CGContextAddPath(ctx, bubblePath);
  CGContextStrokePath(ctx);
  CGPathRelease(bubblePath);

  // Tail.
  CGContextBeginPath(ctx);
  CGContextMoveToPoint(ctx, CGRectGetMinX(bubble) + bubbleW * 0.26, CGRectGetMinY(bubble) + size * 0.012);
  CGContextAddLineToPoint(ctx, CGRectGetMinX(bubble) + bubbleW * 0.16, size * 0.255);
  CGContextAddLineToPoint(ctx, CGRectGetMinX(bubble) + bubbleW * 0.46, CGRectGetMinY(bubble) + size * 0.012);
  CGContextSetRGBFillColor(ctx, kAccentR, kAccentG, kAccentB, 1.0);
  CGContextFillPath(ctx);

  // Three dots inside the bubble, the "thinking" state the app shows.
  const CGFloat dotRadius = size * 0.030;
  for (int i = -1; i <= 1; ++i) {
    const CGFloat cx = CGRectGetMidX(bubble) + i * bubbleW * 0.24;
    const CGRect dot = CGRectMake(cx - dotRadius, CGRectGetMidY(bubble) - dotRadius,
                                  dotRadius * 2, dotRadius * 2);
    CGContextSetRGBFillColor(ctx, 1, 1, 1, i == 1 ? 0.45 : (i == 0 ? 0.72 : 1.0));
    CGContextFillEllipseInRect(ctx, dot);
  }
}

bool writePng(const std::string& path, int pixels) {
  NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:nullptr
                    pixelsWide:pixels
                    pixelsHigh:pixels
                 bitsPerSample:8
               samplesPerPixel:4
                      hasAlpha:YES
                      isPlanar:NO
                colorSpaceName:NSCalibratedRGBColorSpace
                   bytesPerRow:pixels * 4
                  bitsPerPixel:32];
  NSGraphicsContext* context = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
  [NSGraphicsContext saveGraphicsState];
  [NSGraphicsContext setCurrentContext:context];
  CGContextRef ctx = (CGContextRef)context.CGContext;
  CGContextClearRect(ctx, CGRectMake(0, 0, pixels, pixels));
  drawIcon(ctx, pixels);
  [NSGraphicsContext restoreGraphicsState];

  NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
  return [png writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES];
}

}  // namespace

int main(int argc, const char* argv[]) {
  @autoreleasepool {
    if (argc < 2) {
      fprintf(stderr, "usage: make_icon <iconset directory>\n");
      return 1;
    }
    const std::string dir = argv[1];
    struct Entry {
      int points;
      int scale;
    };
    const std::vector<Entry> entries = {{16, 1}, {16, 2}, {32, 1},  {32, 2},  {128, 1},
                                        {128, 2}, {256, 1}, {256, 2}, {512, 1}, {512, 2}};
    for (const Entry& entry : entries) {
      char name[128];
      snprintf(name, sizeof(name), "%s/icon_%dx%d%s.png", dir.c_str(), entry.points,
               entry.points, entry.scale == 2 ? "@2x" : "");
      if (!writePng(name, entry.points * entry.scale)) {
        fprintf(stderr, "failed to write %s\n", name);
        return 1;
      }
    }
    printf("wrote %zu icon sizes to %s\n", entries.size(), dir.c_str());
  }
  return 0;
}
