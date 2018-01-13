#include <stdio.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <CoreServices/CoreServices.h>

#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

int main(void) {
  int w = 640;
  int h = 480;
  
  int frames = 60;
  
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGPoint velocity = { 10.0, 10.0 };
  CGFloat radius = 32.0;
  CGRect ellipse = CGRectMake(w/2.0 - radius, h/2.0 - radius, radius*2.0, radius*2.0);

  size_t data_len = w * h * 4;
  void *data = calloc(data_len, 1);
  
  for (int i = 0; i < 60; i++) {
    CGContextRef ctx = CGBitmapContextCreate(data, w, h, 8, w * 4, space, kCGImageAlphaPremultipliedLast);
    
    CGContextSetFillColorWithColor(ctx, CGColorGetConstantColor(kCGColorWhite));
    
    CGContextFillRect(ctx, CGRectMake(0, 0, w, h));
    
    CGContextSetFillColorWithColor(ctx, CGColorGetConstantColor(kCGColorBlack));
    
    CGContextBeginPath(ctx);
    CGContextAddEllipseInRect(ctx, ellipse);
    CGContextFillPath(ctx);
    
    ellipse.origin.x += velocity.x;
    ellipse.origin.y += velocity.y;
    
    if (CGRectGetMaxX(ellipse) >= w || CGRectGetMinX(ellipse) <= 0) {
      velocity.x = -velocity.x;
    } 
    if (CGRectGetMaxY(ellipse) >= h || CGRectGetMinY(ellipse) <= 0) {
      velocity.y = -velocity.y;
    }
    
    ellipse.origin.x = MAX(ellipse.origin.x, 0);
    ellipse.origin.x = MIN(ellipse.origin.x, w - radius * 2.0);
    ellipse.origin.y = MAX(ellipse.origin.y, 0);
    ellipse.origin.y = MIN(ellipse.origin.y, h - radius * 2.0);
    
    char name[255] = { 0 };
    sprintf(name, "testdata/image_%03d.raw", i);
    
    FILE *f = fopen(name, "wb");
    if (!f) {
      perror("Couldn't open file for writing");
      exit(1);
    }
    fwrite(data, data_len, 1, f);
    fclose(f);
        
    CFMutableDataRef pngData = CFDataCreateMutable(NULL, 0);
    CGImageDestinationRef dst = CGImageDestinationCreateWithData(pngData, kUTTypePNG, 1, NULL);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CGImageDestinationAddImage(dst, img, NULL);
    CGImageDestinationFinalize(dst);
    
    sprintf(name, "testdata/image_%03d.png", i);
    f = fopen(name, "wb");
    if (!f) {
      perror("Couldn't open png file for writing");
      exit(1);
    }
    fwrite(CFDataGetBytePtr(pngData), CFDataGetLength(pngData), 1, f);
    fclose(f);
    CFRelease(pngData);
    CFRelease(img);
    CFRelease(dst);
    
    CFRelease(ctx);
  }
  
  return 0;
}
