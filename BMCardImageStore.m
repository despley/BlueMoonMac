/**
 * @file
 * Implementation of class BMCardImageStore.
 */

#import "BMCardImageStore.h"

static BMCardImageStore *g_imageStore = nil;

@implementation BMCardImageStore

#pragma mark Properties

- (CGImageRef)cardBack
{
    if (cardBack == NULL)
    {
        NSImage *image = [NSImage imageNamed:@"cardback.jpg"];
        cardBack = [[[image representations] objectAtIndex:0] CGImage];        
    }
    return cardBack;
}

- (NSSize)cardSize
{
    return [[NSImage imageNamed:@"cardback.jpg"] size];
}

+ (BMCardImageStore *)imageStore
{
    if (!g_imageStore)
        g_imageStore = [[[self class] alloc] init];
    return g_imageStore;
}

- (id)init
{
    if ((self = [super init]))
    {        
    }
    return self;
}

- (void)free
{
    if (self == g_imageStore)
        g_imageStore = nil;
}

/**
 * Returns card image for the specified design.
 * @param d_ptr Pointer to a card design, or NULL.
 */
- (CGImageRef)imageForDesign:(design *)d_ptr
{
    return d_ptr ? [self imageForPeople:d_ptr->people card:d_ptr->index] : self.cardBack;
}

/**
 * Returns card image for the specified people and card index.
 */
- (CGImageRef)imageForPeople:(int)people card:(int)cardIndex
{
    static const char *image_base[] = {
        "hoax",
        "vulca",
        "mimix",
        "flit",
        "khind",
        "terrah",
        "pillar",
        "aqua",
        "buka",
        "mutant"
    };

    CGImageRef *pImage = &image_cache[people][cardIndex];
    if (*pImage == NULL)
    {
        // Construct image name and load image
        NSString *imageName = [NSString stringWithFormat:@"%s%02d.jpg", image_base[people], cardIndex];
        NSImage *image = [NSImage imageNamed:imageName];
        NSAssert([image size].width > 0 && [image size].height > 0, @"Failed to load image");
        *pImage = [[[image representations] objectAtIndex:0] CGImage];

        // Check for error 
        if (*pImage == NULL)
            NSLog(@"Cannot open image %@!\n", imageName);
    }
    return *pImage;
}

@end
