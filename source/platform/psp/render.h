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

// refresh.h -- public interface to refresh functions

#define	MAXCLIPPLANES	11

#define	TOP_RANGE		16			// soldier uniform colors
#define	BOTTOM_RANGE	96

//=============================================================================

typedef struct entity_s
{
	qboolean				forcelink;		// model changed

	int						update_type;

	entity_state_t			baseline;		// to fill in defaults in updates

	double					msgtime;		// time of last update
	vec3_t					msg_origins[2];	// last two updates (0 is newest)
	vec3_t					origin;
	vec3_t					msg_angles[2];	// last two updates (0 is newest)
	vec3_t					angles;

	// Tomaz - QC Alpha Scale Glow Begin
    float		renderamt;
    float		rendermode;
    float		rendercolor[3];
    //Crow_bar

	unsigned char 			scale;
  	struct model_s			*model;			// NULL = no model
  	char					old_model[128];			// NULL = no model
	struct efrag_s			*efrag;			// linked list of efrags
	int						frame;
	float					syncbase;		// for client-side animations
	byte					*colormap;
	int						effects;		// light, particals, etc
	int						skinnum;		// for Alias models
	int						visframe;		// last frame this entity was

	// fenix@io.com: model transform interpolation
    float                   translate_start_time;
    vec3_t                  origin1;
    vec3_t                  origin2;

    float                   rotate_start_time;
    vec3_t                  angles1;
    vec3_t                  angles2;									//  found in an active leaf

	int						dlightframe;	// dynamic lighting
	int						dlightbits;

   int               last_frame;
   int               current_frame;
   float             interpolation;

    	// FIXME: could turn these into a union
	struct mnode_s			*topnode;		// for bmodels, first world node

	int		modelindex;

	float                   frame_start_time;
    float                   frame_interval;
    int                     pose1;
    int                     pose2;

    short                     z_head;
    short                     z_larm;
    short                     z_rarm;

	// for batch drawing entities  
	short next_visedict;
    // fenix@io.com: model transform interpolation
    //  that splits bmodel, or NULL if
    //  not split
} entity_t;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct
{
	vrect_t		vrect;				// subwindow in video for refresh
									// FIXME: not need vrect next field here?
	vrect_t		aliasvrect;			// scaled Alias version
	int			vrectright, vrectbottom;	// right & bottom screen coords
	int			aliasvrectright, aliasvrectbottom;	// scaled Alias versions
	float		vrectrightedge;			// rightmost right edge we care about,
										//  for use in edge list
	float		fvrectx, fvrecty;		// for floating-point compares
	float		fvrectx_adj, fvrecty_adj; // left and top edges, for clamping
	int			vrect_x_adj_shift20;	// (vrect.x + 0.5 - epsilon) << 20
	int			vrectright_adj_shift20;	// (vrectright + 0.5 - epsilon) << 20
	float		fvrectright_adj, fvrectbottom_adj;
										// right and bottom edges, for clamping
	float		fvrectright;			// rightmost edge, for Alias clamping
	float		fvrectbottom;			// bottommost edge, for Alias clamping
	float		horizontalFieldOfView;	// at Z = 1.0, this many X is visible
										// 2.0 = 90 degrees
	float		xOrigin;			// should probably allways be 0.5
	float		yOrigin;			// between be around 0.3 to 0.5

	vec3_t		vieworg;
	vec3_t		viewangles;

	float		fov_x, fov_y;

	int			ambientlight;

    float fog_start;
	float fog_end;
	float fog_red;
	float fog_green;
	float fog_blue;
        //float fog_alpha;

} refdef_t;


//
// refresh
//

extern	refdef_t	r_refdef;
extern vec3_t	r_origin, vpn, vright, vup;

extern	struct texture_s	*r_notexture_mip;


void R_Init (void);
void R_InitTextures (void);
void R_RenderView (void);		// must set r_refdef first
void R_DrawLine(vec3_t start,vec3_t end, vec3_t rgb);//blubs added this

void R_ViewChanged (vrect_t *pvrect, int lineadj, float aspect);
void R_InitSky (byte *mt);	// called at level load

void R_NewMap (void);

// particles

typedef enum trail_type_s
{
	ROCKET_TRAIL, GRENADE_TRAIL, BLOOD_TRAIL, TRACER1_TRAIL, SLIGHT_BLOOD_TRAIL,NAIL_TRAIL,
	TRACER2_TRAIL, VOOR_TRAIL, ALT_ROCKET_TRAIL, LAVA_TRAIL, BUBBLE_TRAIL, NEHAHRA_SMOKE,
	RAYGREEN_TRAIL, RAYRED_TRAIL
} trail_type_t;

void R_ParseParticleEffect (void);
void R_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count);
void R_RocketTrail (vec3_t start, vec3_t end, int type);


void R_DarkFieldParticles (entity_t *ent);
void R_EntityParticles (entity_t *ent);
void R_BlobExplosion (vec3_t org);
void R_ParticleExplosion (vec3_t org);
void R_ParticleExplosion2 (vec3_t org, int colorStart, int colorLength);
void R_LavaSplash (vec3_t org);
void R_TeleportSplash (vec3_t org);

void D_InitCaches (void *buffer, int size);
void R_SetVrect (vrect_t *pvrect, vrect_t *pvrectin, int lineadj);

