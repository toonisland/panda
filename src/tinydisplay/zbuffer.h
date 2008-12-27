#ifndef _tgl_zbuffer_h_
#define _tgl_zbuffer_h_

/*
 * Z buffer
 */

#include "zfeatures.h"
#include "pbitops.h"

typedef unsigned int ZPOINT;
#define ZB_Z_BITS 20
#define ZB_POINT_Z_FRAC_BITS 10  // These must add to < 32.

/* The number of fractional bits below the S and T texture coords.
   The more we have, the more precise the texel calculation will be
   when we zoom into small details of a texture; but the greater
   chance we might overflow our 32-bit integer when texcoords get
   large.  This also limits our greatest texture size (its T dimension
   cannot exceed this number of bits).*/
#define ZB_POINT_ST_FRAC_BITS 12

/* This is the theoretical max number of bits we have available to
   shift down to achieve each next mipmap level, based on the size of
   a 32-bit int.  We need to preallocate mipmap arrays of this size. */
#define MAX_MIPMAP_LEVELS (32 - ZB_POINT_ST_FRAC_BITS + 1)

/* Returns the index within a texture level for the given (s, t) texel. */
#define ZB_TEXEL(level, s, t)                                         \
  ((((t) & (level)->t_mask) >> (level)->t_shift) |                  \
   (((s) & (level)->s_mask) >> ZB_POINT_ST_FRAC_BITS))

#define ZB_LOOKUP_TEXTURE_NEAREST(texture_levels, s, t) \
  (texture_levels)->pixmap[ZB_TEXEL(texture_levels, s, t)]

#define ZB_LOOKUP_TEXTURE_MIPMAP_NEAREST(texture_levels, s, t, level) \
  ZB_LOOKUP_TEXTURE_NEAREST((texture_levels) + (level), (s) >> (level), (t) >> (level))

/* A special abs() function which doesn't require any branching
   instructions.  Might not work on some exotic hardware. */

/* Also doesn't appear to be any faster in practice.  Guess gcc is
   already doing the right thing.  Is msvc? */
//#define FAST_ABS(v) (((v) ^ ((v) >> (sizeof(v) * 8 - 1))) - ((v) >> (sizeof(v) * 8 - 1)))

#define DO_CALC_MIPMAP_LEVEL(mipmap_level, mipmap_dx, dsdx, dtdx) \
  { \
    (mipmap_dx) = ((unsigned int)abs(dsdx) + (unsigned int)abs(dtdx)); \
    (mipmap_level) = get_next_higher_bit((mipmap_dx) >> ZB_POINT_ST_FRAC_BITS); \
    (mipmap_dx) &= ((1 << (((mipmap_level) - 1) + ZB_POINT_ST_FRAC_BITS)) - 1); \
  }

#define ZB_POINT_RED_MIN   0x0000
#define ZB_POINT_RED_MAX   0xffff
#define ZB_POINT_GREEN_MIN 0x0000
#define ZB_POINT_GREEN_MAX 0xffff
#define ZB_POINT_BLUE_MIN  0x0000
#define ZB_POINT_BLUE_MAX  0xffff
#define ZB_POINT_ALPHA_MIN 0x0000
#define ZB_POINT_ALPHA_MAX 0xffff

/* display modes */
#define ZB_MODE_5R6G5B  1  /* true color 16 bits */
#define ZB_MODE_INDEX   2  /* color index 8 bits */
#define ZB_MODE_RGBA    3  /* 32 bit rgba mode */
#define ZB_MODE_RGB24   4  /* 24 bit rgb mode */
#define ZB_NB_COLORS    225 /* number of colors for 8 bit display */

#define RGB_TO_PIXEL(r,g,b) \
  ((((unsigned int)(r) << 8) & 0xff0000) | ((unsigned int)(g) & 0xff00) | ((unsigned int)(b) >> 8))
#define RGBA_TO_PIXEL(r,g,b,a)                                   \
  ((((unsigned int)(a) << 16) & 0xff000000) | (((unsigned int)(r) << 8) & 0xff0000) | ((unsigned int)(g) & 0xff00) | ((unsigned int)(b) >> 8))
#define PIXEL_R(p) (((unsigned int)(p) & 0xff0000) >> 8)
#define PIXEL_G(p) ((unsigned int)(p) & 0xff00)
#define PIXEL_B(p) (((unsigned int)(p) & 0x00ff) << 8)
#define PIXEL_A(p) (((unsigned int)(p) & 0xff000000) >> 16)
typedef unsigned int PIXEL;
#define PSZB 4
#define PSZSH 5

// Returns an unsigned product of c1 * c2
#define PCOMPONENT_MULT(c1, c2) \
  ((((unsigned int)(c1) * (unsigned int)(c2))) >> 16)
#define PCOMPONENT_MULT3(c1, c2, c3) \
  PCOMPONENT_MULT(c1, PCOMPONENT_MULT(c2, c3))
#define PCOMPONENT_MULT4(c1, c2, c3, c4) \
  PCOMPONENT_MULT(PCOMPONENT_MULT(c1, c2), PCOMPONENT_MULT(c3, c4))

// Returns a signed product of c1 * c2, where c1 is initially signed.
// We leave 2 bits on the top to differentiate between c1 < 0 and c1 >
// 0xffff; the result has the same sign.
#define PALPHA_MULT(c1, c2) \
  (((int)(((int)(c1) >> 2) * (unsigned int)(c2))) >> 14)

#define PCOMPONENT_BLEND(c1, c2, a2) \
  ((((unsigned int)(c1) * ((unsigned int)0xffff - (unsigned int)(a2)) + (unsigned int)(c2) * (unsigned int)(a2))) >> 16)

#define PIXEL_BLEND(r1, g1, b1, r2, g2, b2, a2) \
  RGBA_TO_PIXEL(PCOMPONENT_BLEND(r1, r2, a2),   \
                PCOMPONENT_BLEND(g1, g2, a2),   \
                PCOMPONENT_BLEND(b1, b2, a2),   \
                a2)
#define PIXEL_BLEND_RGB(rgb, r, g, b, a) \
  PIXEL_BLEND(PIXEL_R(rgb), PIXEL_G(rgb), PIXEL_B(rgb), r, g, b, a)

typedef struct {
  PIXEL *pixmap;
  unsigned int s_mask, t_mask, t_shift;
} ZTextureLevel;

typedef struct ZBuffer ZBuffer;
typedef struct ZBufferPoint ZBufferPoint;

typedef void (*ZB_fillTriangleFunc)(ZBuffer  *,
                                    ZBufferPoint *,ZBufferPoint *,ZBufferPoint *);

typedef void (*ZB_storePixelFunc)(ZBuffer *zb, PIXEL &result, int r, int g, int b, int a);

typedef PIXEL (*ZB_lookupTextureFunc)(ZTextureLevel *texture_levels, int s, int t, unsigned int level, unsigned int level_dx);

struct ZBuffer {
  int xsize,ysize;
  int linesize; /* line size, in bytes */
  int mode;
  
  ZPOINT *zbuf;
  PIXEL *pbuf;
  int frame_buffer_allocated;
  
  int nb_colors;
  unsigned char *dctable;
  int *ctable;
  ZTextureLevel *current_textures[MAX_TEXTURE_STAGES];  // This is actually an array of texture levels for each stage.
  int reference_alpha;
  int blend_r, blend_g, blend_b, blend_a;
  ZB_storePixelFunc store_pix_func;
  ZB_lookupTextureFunc tex_minfilter_func;
  ZB_lookupTextureFunc tex_magfilter_func;
};

struct ZBufferPoint {
  int x,y,z;     /* integer coordinates in the zbuffer */
  int s,t;       /* coordinates for the mapping */
  int r,g,b,a;     /* color indexes */
  
  float sz,tz;   /* temporary coordinates for mapping */

  int sa, ta;   /* mapping coordinates for optional second texture stage */
  float sza,tza; 

  int sb, tb;   /* mapping coordinates for optional third texture stage */
  float szb,tzb; 
};

/* zbuffer.c */

#ifdef DO_PSTATS
extern int pixel_count_white_untextured;
extern int pixel_count_flat_untextured;
extern int pixel_count_smooth_untextured;
extern int pixel_count_white_textured;
extern int pixel_count_flat_textured;
extern int pixel_count_smooth_textured;
extern int pixel_count_white_perspective;
extern int pixel_count_flat_perspective;
extern int pixel_count_smooth_perspective;
extern int pixel_count_smooth_multitex2;
extern int pixel_count_smooth_multitex3;

#define COUNT_PIXELS(pixel_count, p0, p1, p2) \
  (pixel_count) += abs((p0)->x * ((p1)->y - (p2)->y) + (p1)->x * ((p2)->y - (p0)->y) + (p2)->x * ((p0)->y - (p1)->y)) / 2

#else

#define COUNT_PIXELS(pixel_count, p0, p1, p2)

#endif  // DO_PSTATS

ZBuffer *ZB_open(int xsize,int ysize,int mode,
		 int nb_colors,
		 unsigned char *color_indexes,
		 unsigned int *color_table,
		 void *frame_buffer);


void ZB_close(ZBuffer *zb);

void ZB_resize(ZBuffer *zb,void *frame_buffer,int xsize,int ysize);
void ZB_clear(ZBuffer *zb, int clear_z, ZPOINT z,
	      int clear_color, unsigned int r, unsigned int g, unsigned int b, unsigned int a);
void ZB_clear_viewport(ZBuffer * zb, int clear_z, ZPOINT z,
                       int clear_color, unsigned int r, unsigned int g, unsigned int b, unsigned int a,
                       int xmin, int ymin, int xsize, int ysize);

PIXEL lookup_texture_nearest(ZTextureLevel *texture_levels, int s, int t, unsigned int level, unsigned int level_dx);
PIXEL lookup_texture_bilinear(ZTextureLevel *texture_levels, int s, int t, unsigned int level, unsigned int level_dx);
PIXEL lookup_texture_mipmap_nearest(ZTextureLevel *texture_levels, int s, int t, unsigned int level, unsigned int level_dx);
PIXEL lookup_texture_mipmap_linear(ZTextureLevel *texture_levels, int s, int t, unsigned int level, unsigned int level_dx);
PIXEL lookup_texture_mipmap_bilinear(ZTextureLevel *texture_levels, int s, int t, unsigned int level, unsigned int level_dx);
PIXEL lookup_texture_mipmap_trilinear(ZTextureLevel *texture_levels, int s, int t, unsigned int level, unsigned int level_dx);

/* linesize is in BYTES */
void ZB_copyFrameBuffer(ZBuffer *zb,void *buf,int linesize);

/* zdither.c */

void ZB_initDither(ZBuffer *zb,int nb_colors,
		   unsigned char *color_indexes,int *color_table);
void ZB_closeDither(ZBuffer *zb);
void ZB_ditherFrameBuffer(ZBuffer *zb,unsigned char *dest,
			  int linesize);

/* zline.c */

void ZB_plot(ZBuffer *zb,ZBufferPoint *p);
void ZB_line(ZBuffer *zb,ZBufferPoint *p1,ZBufferPoint *p2);
void ZB_line_z(ZBuffer * zb, ZBufferPoint * p1, ZBufferPoint * p2);


/* memory.c */
void gl_free(void *p);
void *gl_malloc(int size);
void *gl_zalloc(int size);

#endif /* _tgl_zbuffer_h_ */
