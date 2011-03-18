/**
 * @file
 * Interface of class BMCardImageStore.
 */

#import <Cocoa/Cocoa.h>
#import "bluemoon.h"

#define kNumPeople 10   ///< Amount of various people in the game.
#define kNumCards 128   ///< Largest possible card index in the game.

@interface BMCardImageStore : NSObject
{
    /// Images for the card faces
    CGImageRef image_cache[kNumPeople][kNumCards];

    /// Image used for card backgrounds
    CGImageRef cardBack;
}

@property(readonly) NSSize cardSize;
@property(readonly) CGImageRef cardBack;

+ (BMCardImageStore *)imageStore;
- (void)free;
- (CGImageRef)imageForDesign:(design *)d_ptr;
- (CGImageRef)imageForPeople:(int)people card:(int)cardIndex;

@end
