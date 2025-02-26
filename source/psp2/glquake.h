/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// disable data conversion warnings

#pragma warning(disable : 4244)     // MIPS
#pragma warning(disable : 4136)     // X86
#pragma warning(disable : 4051)     // ALPHA
  
#ifdef _WIN32
#include <windows.h>
#endif

#include <vitaGL.h>

extern GLint monocolor;
extern GLint modulcolor[2];

extern float costablef[17];
extern float sintablef[17];

void GL_DrawFPS(void);
void GL_Set2D (void);
void GL_Color(float r, float g, float b, float a);
void GL_DrawPolygon(GLenum prim, int num);
void GL_BeginRendering (int *x, int *y, int *width, int *height);
void GL_EndRendering (void);
void GL_EnableState(GLenum state);
void GL_DisableState(GLenum state);
int loadtextureimage (char* filename, int matchwidth, int matchheight, qboolean complain, int filter);

#ifdef _WIN32
// Function prototypes for the Texture Object Extension routines
typedef GLboolean (APIENTRY *ARETEXRESFUNCPTR)(GLsizei, const GLuint *,
                    const GLboolean *);
typedef void (APIENTRY *BINDTEXFUNCPTR)(GLenum, GLuint);
typedef void (APIENTRY *DELTEXFUNCPTR)(GLsizei, const GLuint *);
typedef void (APIENTRY *GENTEXFUNCPTR)(GLsizei, GLuint *);
typedef GLboolean (APIENTRY *ISTEXFUNCPTR)(GLuint);
typedef void (APIENTRY *PRIORTEXFUNCPTR)(GLsizei, const GLuint *,
                    const GLclampf *);
typedef void (APIENTRY *TEXSUBIMAGEPTR)(int, int, int, int, int, int, int, int, void *);

extern	BINDTEXFUNCPTR bindTexFunc;
extern	DELTEXFUNCPTR delTexFunc;
extern	TEXSUBIMAGEPTR TexSubImage2DFunc;
#endif

extern	int texture_extension_number;
extern	int		texture_mode;

extern	float	gldepthmin, gldepthmax;

void GL_Upload32 (unsigned *data, int width, int height,  bool mipmap, bool alpha);
void GL_Upload8 (byte *data, int width, int height,  bool mipmap, bool alpha);
int GL_LoadTexture (const char *identifier, int width, int height, byte *data, bool mipmap, bool alpha);
int GL_LoadTexture32 (const char *identifier, int width, int height, byte *data, bool mipmap, bool alpha, bool fullbright);

extern	qboolean	qmb_initialized;

typedef struct
{
	float	x, y, z;
	float	s, t;
	float	r, g, b;
} glvert_t;

extern glvert_t glv;

extern	int glx, gly, glwidth, glheight;

#ifdef _WIN32
extern	PROC glArrayElementEXT;
extern	PROC glColorPointerEXT;
extern	PROC glTexturePointerEXT;
extern	PROC glVertexPointerEXT;
#endif

/*
---------------------------------
half-life Render Modes. Crow_bar
---------------------------------
*/

#define TEX_COLOR    1
#define TEX_TEXTURE  2
#define TEX_GLOW     3
#define TEX_SOLID    4
#define TEX_ADDITIVE 5
#define TEX_LMPOINT  6 //for light point

#define ISCOLOR(ent)    ((ent)->rendermode == TEX_COLOR    && ((ent)->rendercolor[0] <= 1|| \
                                                               (ent)->rendercolor[1] <= 1|| \
															   (ent)->rendercolor[2] <= 1))

#define ISTEXTURE(ent)  ((ent)->rendermode == TEX_TEXTURE  && (ent)->renderamt > 0 && (ent)->renderamt <= 1)
#define ISGLOW(ent)     ((ent)->rendermode == TEX_GLOW     && (ent)->renderamt > 0 && (ent)->renderamt <= 1)
#define ISSOLID(ent)    ((ent)->rendermode == TEX_SOLID    && (ent)->renderamt > 0 && (ent)->renderamt <= 1)
#define ISADDITIVE(ent) ((ent)->rendermode == TEX_ADDITIVE && (ent)->renderamt > 0 && (ent)->renderamt <= 1)

#define ISLMPOINT(ent)  ((ent)->rendermode == TEX_LMPOINT  && ((ent)->rendercolor[0] <= 1|| \
                                                               (ent)->rendercolor[1] <= 1|| \
															   (ent)->rendercolor[2] <= 1))
/*
---------------------------------
//half-life Render Modes
---------------------------------
*/

// r_local.h -- private refresh defs

#define ALIAS_BASE_SIZE_RATIO		(1.0 / 11.0)
					// normalizing factor so player model works out to about
					//  1 pixel per triangle
#define	MAX_LBM_HEIGHT		480

#define TILE_SIZE		128		// size of textures generated by R_GenTiledSurf

#define SKYSHIFT		7
#define	SKYSIZE			(1 << SKYSHIFT)
#define SKYMASK			(SKYSIZE - 1)

#define BACKFACE_EPSILON	0.01

// Fragment shaders
#define MODULATE_WITH_COLOR  0
#define MODULATE             1
#define REPLACE              2
#define MONO_COLOR           4
#define MODULATE_COLOR_A     5
#define MODULATE_A           6
#define RGBA_A               7
#define REPLACE_A            8

// Vertex shaders
#define TEXTURE2D            0
#define TEXTURE2D_WITH_COLOR 1
#define COLOR                2
#define VERTEX_ONLY          3

// Shader programs
#define TEX2D_REPL      0
#define TEX2D_MODUL     1
#define TEX2D_MODUL_CLR 2
#define RGBA_COLOR      3
#define NO_COLOR        4
#define TEX2D_REPL_A    5
#define TEX2D_MODUL_A   6
#define RGBA_CLR_A      7
#define FULL_A          8


void R_TimeRefresh_f (void);
void R_ReadPointFile_f (void);
texture_t *R_TextureAnimation (texture_t *base);

typedef struct surfcache_s
{
	struct surfcache_s	*next;
	struct surfcache_s 	**owner;		// NULL is an empty chunk of memory
	int					lightadj[MAXLIGHTMAPS]; // checked for strobe flush
	int					dlight;
	int					size;		// including header
	unsigned			width;
	unsigned			height;		// DEBUG only needed for debug
	float				mipscale;
	struct texture_s	*texture;	// checked for animating textures
	byte				data[4];	// width*height elements
} surfcache_t;


typedef struct
{
	pixel_t		*surfdat;	// destination for generated surface
	int			rowbytes;	// destination logical width in bytes
	msurface_t	*surf;		// description for surface to generate
	fixed8_t	lightadj[MAXLIGHTMAPS];
							// adjust for lightmap levels for dynamic lighting
	texture_t	*texture;	// corrected for animating textures
	int			surfmip;	// mipmapped ratio of surface texels / world pixels
	int			surfwidth;	// in mipmapped texels
	int			surfheight;	// in mipmapped texels
} drawsurf_t;


typedef enum {
	pt_static, pt_grav, pt_slowgrav, pt_fire, pt_explode, pt_explode2, pt_blob, pt_blob2
} ptype_t;

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct particle_s
{
// driver-usable fields
	vec3_t		org;
	float		color;
// drivers never touch the following fields
	struct particle_s	*next;
	vec3_t		vel;
	float		ramp;
	float		die;
	ptype_t		type;
} particle_t;


//====================================================


extern	entity_t	r_worldentity;
extern	bool	r_cache_thrash;		// compatability
extern	vec3_t		modelorg, r_entorigin;
extern	entity_t	*currententity;
extern	int			r_visframecount;	// ??? what difs?
extern	int			r_framecount;
extern	mplane_t	frustum[4];
extern	int		c_brush_polys, c_alias_polys;


//
// view origin
//
extern	vec3_t	vup;
extern	vec3_t	vpn;
extern	vec3_t	vright;
extern	vec3_t	r_origin;

//
// screen size info
//
extern	refdef_t	r_refdef;
extern	mleaf_t		*r_viewleaf, *r_oldviewleaf;
extern	texture_t	*r_notexture_mip;
extern	int		d_lightstylevalue[256];	// 8.8 fraction of base light value

extern	bool	envmap;
extern	int	currenttexture;
extern	int	cnttextures[2];
extern	int	particletexture;
extern	int	playertextures;

extern	int	skytexturenum;		// index in cl.loadmodel, not gl texture object

extern	cvar_t	r_norefresh;
extern	cvar_t	r_drawentities;
extern	cvar_t	r_drawworld;
extern	cvar_t	r_drawviewmodel;
extern	cvar_t	r_speeds;
extern	cvar_t	r_waterwarp;
extern	cvar_t	r_fullbright;
extern	cvar_t	r_lightmap;
extern	cvar_t	r_shadows;
extern	cvar_t	r_mirroralpha;
extern	cvar_t	r_wateralpha;
extern	cvar_t	r_dynamic;
extern	cvar_t	r_novis;

extern  cvar_t  r_laserpoint;
extern  cvar_t  r_particle_count;
extern  cvar_t	r_part_explosions;
extern  cvar_t	r_part_trails;
extern  cvar_t	r_part_sparks;
extern  cvar_t  r_part_spikes;
extern  cvar_t	r_part_gunshots;
extern  cvar_t	r_part_blood;
extern  cvar_t	r_part_telesplash;
extern  cvar_t	r_part_blobs;
extern  cvar_t	r_part_lavasplash;
extern	cvar_t	r_part_flames;
extern	cvar_t	r_part_lightning;
extern	cvar_t	r_part_flies;
extern	cvar_t	r_bounceparticles;
extern	cvar_t  r_explosiontype;
extern  cvar_t	r_part_muzzleflash;
extern  cvar_t	r_flametype;
extern  cvar_t	r_bounceparticles;
extern  cvar_t	r_decal_blood;
extern  cvar_t	r_decal_bullets;
extern  cvar_t	r_decal_sparks;
extern  cvar_t	r_decal_explosions;
extern  cvar_t  r_coronas;
extern  cvar_t  r_model_brightness;

extern	cvar_t	gl_clear;
extern	cvar_t	gl_cull;
extern	cvar_t	gl_poly;
extern	cvar_t	gl_texsort;
extern	cvar_t	gl_smoothmodels;
extern	cvar_t	gl_affinemodels;
extern	cvar_t	gl_polyblend;
extern	cvar_t	gl_keeptjunctions;
extern	cvar_t	gl_reporttjunctions;
extern	cvar_t	gl_flashblend;
extern	cvar_t	gl_nocolors;
extern	cvar_t	gl_doubleeyes;
extern	cvar_t	gl_xflip;
extern	cvar_t	gl_overbright;

extern	int		gl_lightmap_format;
extern	int		gl_solid_format;
extern	int		gl_alpha_format;

extern	cvar_t	gl_max_size;
extern	cvar_t	gl_playermip;

extern	int			mirrortexturenum;	// quake texturenum, not gltexturenum
extern	bool	mirror;
extern	mplane_t	*mirror_plane;

extern	float	r_world_matrix[16];

extern	const char *gl_vendor;
extern	const char *gl_renderer;
extern	const char *gl_version;
extern	const char *gl_extensions;

// Stereo
extern  cvar_t  st_separation;
extern  cvar_t  st_zeropdist;
extern  cvar_t  st_fustbal;
//end stereo

void R_TranslatePlayerSkin (int playernum);
void GL_Bind (int texnum);

#ifndef _WIN32
#define APIENTRY /* */
#endif

// fenix@io.com: model interpolation
extern  cvar_t  r_interpolate_model_animation;
extern  cvar_t  r_interpolate_model_transform;

// naievil -- fixme: none of these work
//-----------------------------------------------------
void QMB_InitParticles (void);
void QMB_ClearParticles (void);
void QMB_DrawParticles (void);
void QMB_Q3TorchFlame (vec3_t org, float size);
void QMB_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count);
void QMB_RocketTrail (vec3_t start, vec3_t end, trail_type_t type);
void QMB_BlobExplosion (vec3_t org);
void QMB_ParticleExplosion (vec3_t org);
void QMB_LavaSplash (vec3_t org);
void QMB_TeleportSplash (vec3_t org);
void QMB_InfernoFlame (vec3_t org);
void QMB_StaticBubble (entity_t *ent);
void QMB_ColorMappedExplosion (vec3_t org, int colorStart, int colorLength);
void QMB_TorchFlame (vec3_t org);
void QMB_FlameGt (vec3_t org, float size, float time);
void QMB_BigTorchFlame (vec3_t org);
void QMB_ShamblerCharge (vec3_t org);
void QMB_LightningBeam (vec3_t start, vec3_t end);
//void QMB_GenSparks (vec3_t org, byte col[3], float count, float size, float life);
void QMB_EntityParticles (entity_t *ent);
void QMB_MuzzleFlash (vec3_t org);
void QMB_MuzzleFlashLG (vec3_t org);
void QMB_Q3Gunshot (vec3_t org, int skinnum, float alpha);
void QMB_Q3Teleport (vec3_t org, float alpha);
void QMB_Q3TorchFlame (vec3_t org, float size);