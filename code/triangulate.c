#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#include "font_data.h"
#include "opentype.h"
#include "linkedlist.h"

/* The contour triangulation process

Oletukset
- Mikään käyrä ei leikkaa toista käyrää

1. Joka ääriviivalle A
	Kerää pisteistä linkitetty lista
	Pätki A:n pisteet niin, ettei off-curve pisteitä ole kahta peräkkäin
	A.ulko = Etsi A:n pisteistä se ääriviiva B, joka sisältää kaikki A:n pisteet
	A.sisä = Etsi A:n pisteistä se ääriviiva C, jonka sisällä ei ole yhtään A:n pistettä
	for each off-curve point in B:
		kirjoita kupera kolmio
	for each off-curve point in C:
		kirjoita kovera kolmio
	Joka ääriviivalle D:
		jos D:n 1. piste on C:n sisäpuolella:
			A.lapset += [D]

2. Joka ääriviivalle A
	jos A.lapset on tyhjä:
		muodosta kolmioverkko (A.sisä)
	else:
		for B in A.lapset:
			jos leikkaavat( A.sisä, B.ulko )
				subdivide A ja B leikkauspisteen kohdalta
		// muodosta kolmioverkko (leikkaamattomien) ääriviivojen (A.sisä) ja (A.lapset[]) välille
*/

enum {
	/* memory limits */
	MAX_CONTOURS = 128, /* max contours per glyph (FreeMono.ttf has 120) */
	MAX_HOLES = 32, /* max holes per polygon */
	MAX_POINTS = 512 /* max points per contour (FreeMono.ttf has 480) */
};

typedef struct {
	LinkedList points;
	uint8 depth;
	uint8 num_holes;
	uint8 holes[MAX_HOLES]; /* indices of other contours */
} Contour;

/* Returns some point that is on the contour or ~0 on failure */
static uint16 get_on_curve_point( Contour c[1], uint8 flags[] )
{
	uint16 pt = 0xFFFF;
	if ( c->points.length > 0 )
	{
		LLNodeID cur, start;
		cur = start = c->points.root;
		do {
			if ( flags[ cur ] & PT_ON_CURVE )
			{
				pt = cur;
				break;
			}
			cur = LL_NEXT( c->points, cur );
		} while( cur != start );
	}
	return pt;
}

static void delta_vec( float out[2], float const a[2], float const b[2] )
{
	out[0] = b[0] - a[0];
	out[1] = b[1] - a[1];
}

/*
static void get_midpoint( float c[2], float const a[2], float const b[2] )
{
	c[0] = ( a[0] + b[0] ) / 2;
	c[1] = ( a[1] + b[1] ) / 2;
}
*/

/* Returns Z-component of the resulting vector (because X and Y would be zero) */
static float cross2( float const a[2], float const b[2] ) {
	return a[0] * b[1] - a[1] * b[0];
}

static int split_consecutive_off_curve_points( Contour *co, float coords[], uint8 point_flags[] )
{
	LLNodeID a, b, c, d, start;
	
	a = start = co->points.root;
	
	do {
		b = LL_NEXT( co->points, a );
		c = LL_NEXT( co->points, b );
		d = LL_NEXT( co->points, c );
		
		if ( ( point_flags[a] & PT_ON_CURVE )
		&& !( point_flags[b] & PT_ON_CURVE )
		&& !( point_flags[c] & PT_ON_CURVE )
		&& ( point_flags[d] & PT_ON_CURVE ) )
		{
			/* on-off-off-on
			
			If both off-curve points are on the same side of line AD, then they can be merged
			Otherwise, a new point must be added
			*/
			
			float *pa = coords + 2*a;
			float *pb = coords + 2*b;
			float *pc = coords + 2*c;
			float *pd = coords + 2*d;
			
			float vab[2], vac[2], vad[2];
			
			delta_vec( vab, pa, pb );
			delta_vec( vac, pa, pc );
			delta_vec( vad, pa, pd );
			
			if ( cross2( vad, vab ) * cross2( vad, vac ) > 0 )
			{
				/* points B and C are on the same side of line AD. Therefore they can be merged */
				printf( "%s:%s: todo: merge points\n", __FILE__, __func__ );
			}
			else
			{
				/* On different sides. A new point is needed.
				Some fonts don't have even a single case of this point configuration, other fonts have a few */
				printf( "%s:%s: todo: add a point\n", __FILE__, __func__ );
			}
			return 0;
		}
		
		a = b;
	} while( a != start );
	
	return 1;
}

static int contour_contains_point( Contour *c, float p[2] )
{
	/* todo */
	(void) c;
	(void) p;
	return 0;
}

int triangulate_contours( GlyphTriangles *gt, uint8 point_flags[], float points[], uint16 end_points[], uint32 num_contours )
{
	LLNode con_node_pools[MAX_CONTOURS][MAX_POINTS];
	Contour con[MAX_CONTOURS];
	uint16 start=0, end, c, d, p;
	
	/* Construct a linked list for each contour */
	for( c=0; c<num_contours; c++ )
	{
		end = end_points[c];
		
		if ( end > MAX_POINTS )
			return 0;
		
		init_list( &con[c].points, con_node_pools[c], MAX_POINTS );
		con[c].depth = 0;
		con[c].num_holes = 0;
		
		for( p=start; p<=end; p++ )
			add_node_x( &con[c].points, p );
		
		start = end;
	}
	
	/* 1. Preprocess */
	for( c=0; c<num_contours; c++ )
	{
		Contour *c1 = con + c;
		
		/* Make sure that there are no 2 consecutive off-curve points anywhere */
		if ( !split_consecutive_off_curve_points( c1, points, point_flags ) )
		{
			/* Invalid geometry */
			return 0;
		}
		
		/* Determine which contour contains which contour */
		for( d=0; d<num_contours; d++ )
		{
			Contour *c2 = con + d;
			
			if ( c1== c2 )
				continue;
			
			/*
			If c1 contains at least 1 point of c2 then c1 must contain all the points of c2.
			This is because we assume that no contours intersect.
			*/
			
			p = get_on_curve_point( c2, point_flags );
			
			if ( p == 0xFFFF ) {
				/* c2 doesn't contain any on-curve points and is therefore invalid. can not triangulate corrupt shit */
				return 0;
			}
			
			if ( contour_contains_point( c1, points + 2 * p ) )
			{
				if ( c1->num_holes == MAX_HOLES ) {
					/* too many holes */
					return 0;
				} else {
					c1->holes[ c1->num_holes++ ] = d;
					c2->depth++;
				}
			}
		}
	}
	
	/* 2. Triangulate */
	for( c=0; c<num_contours; c++ )
	{
		if ( ( con[c].depth & 1 ) == 0 )
		{
			/* The contour is an exterior (ulkoreuna)
			todo:
				- find the interior polygon of this contour, adding triangles at curves in the process
			*/
			
			if ( con[c].num_holes )
			{
				/* A solid blob which contains holes
				todo:
					- triangulate a (potentially concave) polygon with (potentially concave) holes in it
					- test if this contour intersects any of the holes and subdivide the intersections
					- find the exterior polygons of the holes
					(access holes via con[c].num_holes and con[c].holes)
				*/
			}
			else
			{
				/* A solid blob
				todo:
					triangulate a (potentially concave) polygon
				*/
			}
		}
		else
		{
			/* The contour is an interior (sisäreuna)
			This case is already handled above */
		}
	}
	
	gt->num_points_total = gt->num_points_orig = end_points[ num_contours - 1 ] + 1;
	gt->num_indices_total = 0;
	gt->num_indices_convex = 0;
	gt->num_indices_concave = 0;
	gt->num_indices_solid = 0;
	gt->end_points = end_points;
	gt->indices = NULL;
	gt->points = points;
	
	return 1;
}
