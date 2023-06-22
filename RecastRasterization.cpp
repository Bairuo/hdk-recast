/*
* Houdini tools based on HDK and Recast(Epic Games modified version).
 *
 * Copyright (c) 
 *	2021 Side Effects Software Inc.
 *	Epic Games, Inc.
 *	2009-2010 Mikko Mononen memon@inside.org
 *	2023 Bairuo https://www.zhihu.com/people/Bairuo
 *
 * Redistribution and use of hdk-recast in source and
 * 
 * binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. The name of Side Effects Software may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY SIDE EFFECTS SOFTWARE `AS IS' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL SIDE EFFECTS SOFTWARE BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *----------------------------------------------------------------------------
 */

#include <corecrt_math.h>

#include "Recast.h"
#include "RecastMath.h"
#include "RecastAlloc.h"

#define TEST_NEW_RASTERIZER (0)

static rcSpan* allocSpan(rcHeightfield& hf)
{
	// If running out of memory, allocate new page and update the freelist.
	if (!hf.freelist || !hf.freelist->next)
	{
		// Create new page.
		// Allocate memory for the new pool.
		rcSpanPool* pool = (rcSpanPool*)rcAlloc(sizeof(rcSpanPool), RC_ALLOC_PERM);
		if (!pool) return 0;
		pool->next = 0;
		// Add the pool into the list of pools.
		pool->next = hf.pools;
		hf.pools = pool;
		// Add new items to the free list.
		rcSpan* freelist = hf.freelist;
		rcSpan* head = &pool->items[0];
		rcSpan* it = &pool->items[RC_SPANS_PER_POOL];
		do
		{
			--it;
			it->next = freelist;
			freelist = it;
		}
		while (it != head);
		hf.freelist = it;
	}
	
	// Pop item from in front of the free list.
	rcSpan* it = hf.freelist;
	hf.freelist = hf.freelist->next;
	return it;
}

static void freeSpan(rcHeightfield& hf, rcSpan* ptr)
{
	if (!ptr) return;
	// Add the node in front of the free list.
	ptr->next = hf.freelist;
	hf.freelist = ptr;
}

static void addSpan(rcHeightfield& hf, const int x, const int y,
					const unsigned short smin, const unsigned short smax,
					const unsigned char area, const int flagMergeThr)
{
	
	int idx = x + y*hf.width;
	
	rcSpan* s = allocSpan(hf);
	s->data.smin = smin;
	s->data.smax = smax;
	s->data.area = area;
	s->next = 0;
	
	// Empty cell, add the first span.
	if (!hf.spans[idx])
	{
		hf.spans[idx] = s;
		return;
	}
	rcSpan* prev = 0;
	rcSpan* cur = hf.spans[idx];
	
	// Insert and merge spans.
	while (cur)
	{
		if (cur->data.smin > s->data.smax)
		{
			// Current span is further than the new span, break.
			break;
		}
		else if (cur->data.smax < s->data.smin)
		{
			// Current span is before the new span advance.
			prev = cur;
			cur = cur->next;
		}
		else
		{
			// @UE4 BEGIN
			// Merge overlapping spans.

			// For spans whose tops are really close to each other, prefer walkable areas.
			// This is done in order to remove aliasing (similar to z-fighting) on surfaces close to each other.
			if (rcAbs((int)s->data.smax - (int)cur->data.smax) <= flagMergeThr)
			{
				s->data.area = rcMax(s->data.area, cur->data.area);
			}
			else
			{
				// Use the new spans area if it will become the top.
				if (cur->data.smax > s->data.smax)
					s->data.area = cur->data.area;
			}

			// Merge height intervals.
			if (cur->data.smin < s->data.smin)
				s->data.smin = cur->data.smin;
			if (cur->data.smax > s->data.smax)
				s->data.smax = cur->data.smax;
			// @UE4 END
			
			// Remove current span.
			rcSpan* next = cur->next;
			freeSpan(hf, cur);
			if (prev)
				prev->next = next;
			else
				hf.spans[idx] = next;
			cur = next;
		}
	}
	
	// Insert new span.
	if (prev)
	{
		s->next = prev->next;
		prev->next = s;
	}
	else
	{
		s->next = hf.spans[idx];
		hf.spans[idx] = s;
	}
}

static inline void addFlatSpanSample(rcHeightfield& hf, const int x, const int y)
{
	hf.RowExt[y + 1].MinCol = intMin(hf.RowExt[y + 1].MinCol, x);
	hf.RowExt[y + 1].MaxCol = intMax(hf.RowExt[y + 1].MaxCol, x);
}

static inline void intersectX(const float* v0, const float* edge, float cx, float *pnt)
{
	float t = rcClamp((cx - v0[0]) * edge[9 + 0], 0.0f, 1.0f);  // inverses

	pnt[0] = v0[0] + t * edge[0];
	pnt[1] = v0[1] + t * edge[1];
	pnt[2] = v0[2] + t * edge[2];
}

static inline int SampleIndex(rcHeightfield const& hf, const int x, const int y)
{
#if TEST_NEW_RASTERIZER
	rcAssert(x >= -1 && x < hf.width + 1 && y >= -1 && y < hf.height + 1);
#endif
	return x + 1 + (y + 1)*(hf.width + 2);
}

static inline void intersectZ(const float* v0, const float* edge, float cz, float *pnt)
{
	float t = rcClamp((cz - v0[2]) * edge[9 + 2], 0.0f, 1.0f); //inverses

	pnt[0] = v0[0] + t * edge[0];
	pnt[1] = v0[1] + t * edge[1];
	pnt[2] = v0[2] + t * edge[2];
}

static inline void addSpanSample(rcHeightfield& hf, const int x, const int y, short int sint)
{
	addFlatSpanSample(hf, x, y);
	int idx = SampleIndex(hf, x, y);
	rcTempSpan& Temp = hf.tempspans[idx];

	Temp.sminmax[0] = Temp.sminmax[0] > sint ? sint : Temp.sminmax[0];
	Temp.sminmax[1] = Temp.sminmax[1] < sint ? sint : Temp.sminmax[1];
}

void rasterizeTri(const float* v0, const float* v1, const float* v2,
						 const unsigned char area, rcHeightfield& hf,
						 const float* bmin, const float* bmax,
						 const float cs, const float ics, const float ich, 
						 const int flagMergeThr,
						 const int rasterizationFlags, /*UE4*/
	                     const int* rasterizationMasks /*UE4*/)
{
	rcEdgeHit* const hfEdgeHits = hf.EdgeHits; //this prevents a static analysis warning

	const int w = hf.width;
	const int h = hf.height;
	const float by = bmax[1] - bmin[1];
	const int projectTriToBottom = rasterizationFlags; //UE4

	int intverts[3][2];

	intverts[0][0] = (int)floorf((v0[0] - bmin[0])*ics);
	intverts[0][1] = (int)floorf((v0[2] - bmin[2])*ics);
	intverts[1][0] = (int)floorf((v1[0] - bmin[0])*ics);
	intverts[1][1] = (int)floorf((v1[2] - bmin[2])*ics);
	intverts[2][0] = (int)floorf((v2[0] - bmin[0])*ics);
	intverts[2][1] = (int)floorf((v2[2] - bmin[2])*ics);

	int x0 = intMin(intverts[0][0], intMin(intverts[1][0], intverts[2][0]));
	int x1 = intMax(intverts[0][0], intMax(intverts[1][0], intverts[2][0]));
	int y0 = intMin(intverts[0][1], intMin(intverts[1][1], intverts[2][1]));
	int y1 = intMax(intverts[0][1], intMax(intverts[1][1], intverts[2][1]));

	if (x1 < 0 || x0 >= w || y1 < 0 || y0 >= h)
		return;

	// Calculate min and max of the triangle

	float triangle_smin = rcMin(rcMin(v0[1], v1[1]), v2[1]);
	float triangle_smax = rcMax(rcMax(v0[1], v1[1]), v2[1]);
	triangle_smin -= bmin[1];
	triangle_smax -= bmin[1];
	// Skip the span if it is outside the heightfield bbox
	if (triangle_smax < 0.0f) return;
	if (triangle_smin > by) return;

	if (x0 == x1 && y0 == y1)
	{
		// Clamp the span to the heightfield bbox.
		if (triangle_smin < 0.0f) triangle_smin = 0.0f;
		if (triangle_smax > by) triangle_smax = by;

		// Snap the span to the heightfield height grid.
		unsigned short triangle_ismin = (unsigned short)rcClamp((int)floorf(triangle_smin * ich), 0, RC_SPAN_MAX_HEIGHT);
		unsigned short triangle_ismax = (unsigned short)rcClamp((int)ceilf(triangle_smax * ich), (int)triangle_ismin+1, RC_SPAN_MAX_HEIGHT);
		const int projectSpanToBottom = rasterizationMasks != nullptr ? (projectTriToBottom & rasterizationMasks[x0+y0*w]) : projectTriToBottom;	//UE4
		if (projectSpanToBottom) //UE4
		{
			triangle_ismin = 0; //UE4
		}

		addSpan(hf, x0, y0, triangle_ismin, triangle_ismax, area, flagMergeThr);
		return;
	}

	const short int triangle_ismin = (short int)rcClamp((int)floorf(triangle_smin * ich), -32000, 32000);
	const short int triangle_ismax = (short int)rcClamp((int)floorf(triangle_smax * ich), -32000, 32000);

	x0 = intMax(x0, 0);
	int x1_edge = intMin(x1, w);
	x1 = intMin(x1, w - 1);
	y0 = intMax(y0, 0);
	int y1_edge = intMin(y1, h);
	y1 = intMin(y1, h - 1);
	
	float edges[6][3];

	float vertarray[3][3];
	rcVcopy(vertarray[0], v0);
	rcVcopy(vertarray[1], v1);
	rcVcopy(vertarray[2], v2);

	bool doFlat = true;
	if (doFlat && triangle_ismin == triangle_ismax)
	{
		// flat horizontal, much faster
		for (int basevert = 0; basevert < 3; basevert++)
		{
			int othervert = basevert == 2 ? 0 : basevert + 1;
			int edge = basevert == 0 ? 2 : basevert - 1;

			rcVsub(&edges[edge][0], vertarray[othervert], vertarray[basevert]);
			//rcVnormalize(&edges[edge][0]);
			edges[3 + edge][0] = 1.0f / edges[edge][0];
			edges[3 + edge][1] = 1.0f / edges[edge][1];
			edges[3 + edge][2] = 1.0f / edges[edge][2];

			// drop the vert into the temp span area
			if (intverts[basevert][0] >= x0 && intverts[basevert][0] <= x1 && intverts[basevert][1] >= y0 && intverts[basevert][1] <= y1)
			{
				addFlatSpanSample(hf, intverts[basevert][0], intverts[basevert][1]);
			}
			// set up the edge intersections with horizontal planes
			if (intverts[basevert][1] != intverts[othervert][1])
			{
				int edge0 = intMin(intverts[basevert][1], intverts[othervert][1]);
				int edge1 = intMax(intverts[basevert][1], intverts[othervert][1]);
				int loop0 = intMax(edge0 + 1, y0);
				int loop1 = intMin(edge1, y1_edge);

				unsigned char edgeBits = (edge << 4) | (othervert << 2) | basevert;
				for (int y = loop0; y <= loop1; y++)
				{
					int HitIndex = !!hfEdgeHits[y].Hits[0];
					hfEdgeHits[y].Hits[HitIndex] = edgeBits;
				}
			}
			// do the edge intersections with vertical planes
			if (intverts[basevert][0] != intverts[othervert][0])
			{
				int edge0 = intMin(intverts[basevert][0], intverts[othervert][0]);
				int edge1 = intMax(intverts[basevert][0], intverts[othervert][0]);
				int loop0 = intMax(edge0 + 1, x0);
				int loop1 = intMin(edge1, x1_edge);

				float temppnt[3];
				float cx = bmin[0] + cs * loop0;
				for (int x = loop0; x <= loop1; x++, cx += cs)
				{
					intersectX(vertarray[basevert], &edges[edge][0], cx, temppnt);
					int y = (int)floorf((temppnt[2] - bmin[2])*ics);
					if (y >= y0 && y <= y1)
					{
						addFlatSpanSample(hf, x, y);
						addFlatSpanSample(hf, x - 1, y);
					}
				}
			}
		}
		{
			// deal with the horizontal intersections 
			int edge0 = intMin(intverts[0][1], intMin(intverts[1][1],intverts[2][1]));
			int edge1 = intMax(intverts[0][1], intMax(intverts[1][1],intverts[2][1]));
			int loop0 = intMax(edge0 + 1, y0);
			int loop1 = intMin(edge1, y1_edge);

			float Inter[2][3];
			int xInter[2];

			float cz = bmin[2] + cs * loop0;
			for (int y = loop0; y <= loop1; y++, cz += cs)
			{
				rcEdgeHit& Hits = hfEdgeHits[y];
				if (Hits.Hits[0])
				{
					//rcAssert(Hits.Hits[1]); // must have two hits

					for (int i = 0; i < 2; i++)
					{
						int edge = Hits.Hits[i] >> 4;
						int othervert = (Hits.Hits[i] >> 2) & 3;
						int basevert = Hits.Hits[i] & 3;

						intersectZ(vertarray[basevert], &edges[edge][0], cz, Inter[i]);
						int x = (int)floorf((Inter[i][0] - bmin[0])*ics);
						xInter[i] = x;
						if (x >= x0 && x <= x1)
						{
							addFlatSpanSample(hf, x, y);
							addFlatSpanSample(hf, x, y - 1);
						}
					}
					if (xInter[0] != xInter[1])
					{
						// now fill in the fully contained ones.
						int left = Inter[1][0] < Inter[0][0];  
						int xloop0 = intMax(xInter[left] + 1, x0);
						int xloop1 = intMin(xInter[1 - left], x1);
						if (xloop0 <= xloop1)
						{
							addFlatSpanSample(hf, xloop0, y);
							addFlatSpanSample(hf, xloop1, y);
							addFlatSpanSample(hf, xloop0 - 1, y);
							addFlatSpanSample(hf, xloop1 - 1, y);
							addFlatSpanSample(hf, xloop0, y - 1);
							addFlatSpanSample(hf, xloop1, y - 1);
							addFlatSpanSample(hf, xloop0 - 1, y - 1);
							addFlatSpanSample(hf, xloop1 - 1, y - 1);
						}
					}
					// reset for next triangle
					Hits.Hits[0] = 0;
					Hits.Hits[1] = 0;
				}
			}
		}

		if (rasterizationMasks == nullptr) //UE4
		{
			// Snap the span to the heightfield height grid.
			unsigned short triangle_ismin_clamp = (unsigned short)rcClamp((int)triangle_ismin, 0, RC_SPAN_MAX_HEIGHT);
			const unsigned short triangle_ismax_clamp = (unsigned short)rcClamp((int)triangle_ismax, (int)triangle_ismin_clamp+1, RC_SPAN_MAX_HEIGHT);
			if (projectTriToBottom) //UE4
			{
				triangle_ismin_clamp = 0; //UE4
			}

			for (int y = y0; y <= y1; y++)
			{
				int xloop0 = intMax(hf.RowExt[y + 1].MinCol, x0);
				int xloop1 = intMin(hf.RowExt[y + 1].MaxCol, x1);
				for (int x = xloop0; x <= xloop1; x++)
				{
					addSpan(hf, x, y, triangle_ismin_clamp, triangle_ismax_clamp, area, flagMergeThr);
				}

				// reset for next triangle
				hf.RowExt[y + 1].MinCol = hf.width + 2;
				hf.RowExt[y + 1].MaxCol = -2;
			}
		}
		else
		{
// @UE4 BEGIN
			for (int y = y0; y <= y1; y++)
			{
				int xloop0 = intMax(hf.RowExt[y + 1].MinCol, x0);
				int xloop1 = intMin(hf.RowExt[y + 1].MaxCol, x1);
				for (int x = xloop0; x <= xloop1; x++)
				{
					// Snap the span to the heightfield height grid.
					unsigned short triangle_ismin_clamp = (unsigned short)rcClamp((int)triangle_ismin, 0, RC_SPAN_MAX_HEIGHT);
					const unsigned short triangle_ismax_clamp = (unsigned short)rcClamp((int)triangle_ismax, (int)triangle_ismin_clamp+1, RC_SPAN_MAX_HEIGHT);
					const int projectSpanToBottom = projectTriToBottom & rasterizationMasks[x+y*w];		//UE4
					if (projectSpanToBottom) //UE4
					{
						triangle_ismin_clamp = 0; //UE4
					}
					addSpan(hf, x, y, triangle_ismin_clamp, triangle_ismax_clamp, area, flagMergeThr);
				}

				// reset for next triangle
				hf.RowExt[y + 1].MinCol = hf.width + 2;
				hf.RowExt[y + 1].MaxCol = -2;
			}
// @UE4 END
		}
	}
	else
	{
		//non-flat case
		for (int basevert = 0; basevert < 3; basevert++)
		{
			int othervert = basevert == 2 ? 0 : basevert + 1;
			int edge = basevert == 0 ? 2 : basevert - 1;

			rcVsub(&edges[edge][0], vertarray[othervert], vertarray[basevert]);
			//rcVnormalize(&edges[edge][0]);
			edges[3 + edge][0] = 1.0f / edges[edge][0];
			edges[3 + edge][1] = 1.0f / edges[edge][1];
			edges[3 + edge][2] = 1.0f / edges[edge][2];

			// drop the vert into the temp span area
			if (intverts[basevert][0] >= x0 && intverts[basevert][0] <= x1 && intverts[basevert][1] >= y0 && intverts[basevert][1] <= y1)
			{
				float sfloat = vertarray[basevert][1] - bmin[1];
				short int sint = (short int)rcClamp((int)floorf(sfloat * ich), -32000, 32000);
	#if TEST_NEW_RASTERIZER
				rcAssert(sint >= triangle_ismin - 1 && sint <= triangle_ismax + 1);
	#endif
				addSpanSample(hf, intverts[basevert][0], intverts[basevert][1], sint);
			}
			// set up the edge intersections with horizontal planes
			if (intverts[basevert][1] != intverts[othervert][1])
			{
				int edge0 = intMin(intverts[basevert][1], intverts[othervert][1]);
				int edge1 = intMax(intverts[basevert][1], intverts[othervert][1]);
				int loop0 = intMax(edge0 + 1, y0);
				int loop1 = intMin(edge1, y1_edge);

				unsigned char edgeBits = (edge << 4) | (othervert << 2) | basevert;
				for (int y = loop0; y <= loop1; y++)
				{
					int HitIndex = !!hfEdgeHits[y].Hits[0];
					hfEdgeHits[y].Hits[HitIndex] = edgeBits;
				}
			}
			// do the edge intersections with vertical planes
			if (intverts[basevert][0] != intverts[othervert][0])
			{
				int edge0 = intMin(intverts[basevert][0], intverts[othervert][0]);
				int edge1 = intMax(intverts[basevert][0], intverts[othervert][0]);
				int loop0 = intMax(edge0 + 1, x0);
				int loop1 = intMin(edge1, x1_edge);

				float temppnt[3];
				float cx = bmin[0] + cs * loop0;
				for (int x = loop0; x <= loop1; x++, cx += cs)
				{
					intersectX(vertarray[basevert], &edges[edge][0], cx, temppnt);
					int y = (int)floorf((temppnt[2] - bmin[2])*ics);
					if (y >= y0 && y <= y1)
					{
						float sfloat = temppnt[1] - bmin[1];
						short int sint = (short int)rcClamp((int)floorf(sfloat * ich), -32000, 32000);
#if TEST_NEW_RASTERIZER
						rcAssert(sint >= triangle_ismin - 1 && sint <= triangle_ismax + 1);
#endif
						addSpanSample(hf, x, y, sint);
						addSpanSample(hf, x - 1, y, sint);
					}
				}
			}
		}
		{
			// deal with the horizontal intersections 
			int edge0 = intMin(intverts[0][1], intMin(intverts[1][1],intverts[2][1]));
			int edge1 = intMax(intverts[0][1], intMax(intverts[1][1],intverts[2][1]));
			int loop0 = intMax(edge0 + 1, y0);
			int loop1 = intMin(edge1, y1_edge);

			float Inter[2][3];
			int xInter[2];

			float cz = bmin[2] + cs * loop0;
			for (int y = loop0; y <= loop1; y++, cz += cs)
			{
				rcEdgeHit& Hits = hfEdgeHits[y];
				if (Hits.Hits[0])
				{
					//rcAssert(Hits.Hits[1]); // must have two hits

					for (int i = 0; i < 2; i++)
					{
						int edge = Hits.Hits[i] >> 4;
						int othervert = (Hits.Hits[i] >> 2) & 3;
						int basevert = Hits.Hits[i] & 3;

						//CA_SUPPRESS(6385);
						intersectZ(vertarray[basevert], &edges[edge][0], cz, Inter[i]);
						int x = (int)floorf((Inter[i][0] - bmin[0])*ics);
						xInter[i] = x;
						if (x >= x0 && x <= x1)
						{
							float sfloat = Inter[i][1] - bmin[1];
							short int sint = (short int)rcClamp((int)floorf(sfloat * ich), -32000, 32000);
#if TEST_NEW_RASTERIZER
							rcAssert(sint >= triangle_ismin - 1 && sint <= triangle_ismax + 1);
#endif
							addSpanSample(hf, x, y, sint);
							addSpanSample(hf, x, y - 1, sint);
						}
					}
					if (xInter[0] != xInter[1])
					{
						// now fill in the fully contained ones.
						int left = Inter[1][0] < Inter[0][0];  
						int xloop0 = intMax(xInter[left] + 1, x0);
						int xloop1 = intMin(xInter[1 - left], x1_edge);

						float d = 1.0f / (Inter[1-left][0] - Inter[left][0]);
						float dy = Inter[1-left][1] - Inter[left][1];
						//float ds = dy * d;
						float ds = 0.0f;
						float t = rcClamp((float(xloop0)*cs + bmin[0] - Inter[left][0]) * d, 0.0f, 1.0f);
						float sfloat = (Inter[left][1] + t * dy) - bmin[1];
						if (xloop1 - xloop0 > 0)
						{
							float t2 = rcClamp((float(xloop1)*cs + bmin[0] - Inter[left][0]) * d, 0.0f, 1.0f);
							float sfloat2 = (Inter[left][1] + t2 * dy) - bmin[1];
							ds = (sfloat2 - sfloat) / float(xloop1 - xloop0);
						}
						for (int x = xloop0; x <= xloop1; x++, sfloat += ds)
						{
							short int sint = (short int)rcClamp((int)floorf(sfloat * ich), -32000, 32000);
#if TEST_NEW_RASTERIZER
							rcAssert(sint >= triangle_ismin - 1 && sint <= triangle_ismax + 1);
#endif
							addSpanSample(hf, x, y, sint);
							addSpanSample(hf, x - 1, y, sint);
							addSpanSample(hf, x, y - 1, sint);
							addSpanSample(hf, x - 1, y - 1, sint);
						}
					}
					// reset for next triangle
					Hits.Hits[0] = 0;
					Hits.Hits[1] = 0;
				}
			}
		}
		for (int y = y0; y <= y1; y++)
		{
			int xloop0 = intMax(hf.RowExt[y + 1].MinCol, x0);
			int xloop1 = intMin(hf.RowExt[y + 1].MaxCol, x1);
			for (int x = xloop0; x <= xloop1; x++)
			{
				int idx = SampleIndex(hf, x, y);
				rcTempSpan& Temp = hf.tempspans[idx];

				short int smin = Temp.sminmax[0];
				short int smax = Temp.sminmax[1];
	#if TEST_NEW_RASTERIZER
				short int tsmin = Temp.sminmax[0];
				short int tsmax = Temp.sminmax[1];
	#endif
				// reset for next triangle
				Temp.sminmax[0] = 32000;
				Temp.sminmax[1] = -32000;

				// Skip the span if it is outside the heightfield bbox
				if (smin >= RC_SPAN_MAX_HEIGHT || smax < 0) continue;

				smin = intMax(smin, 0);
				smax = intMin(intMax(smax,smin+1), RC_SPAN_MAX_HEIGHT);
				const int projectSpanToBottom = rasterizationMasks != nullptr ? (projectTriToBottom & rasterizationMasks[x+y*w]) : projectTriToBottom; //UE4
				if (projectSpanToBottom) //UE4
				{
					smin = 0; //UE4
				}

	#if TEST_NEW_RASTERIZER
				{
					short int outsmin, outsmax;
					rasterizeTriTest(v0, v1, v2, x, y, 
						outsmin, outsmax, 
						area, hf,
						bmin, bmax,
						cs, ics, ich,
						flagMergeThr);
					const int tol = 1;
					if (outsmin > smin + tol || outsmin < smin - tol ||
						outsmax > smax + tol || outsmax < smax - tol
						)
					{
						Temp.sminmax[0] = 32000;
						Temp.sminmax[1] = -32000;
						rasterizeTriTest(v0, v1, v2, x, y, 
							outsmin, outsmax, 
							area, hf,
							bmin, bmax,
							cs, ics, ich,
							flagMergeThr);
						if (outsmin != 8191)
						{
							Temp.sminmax[0] = 32000;
							Temp.sminmax[1] = -32000;
						}
					}

				}
	#endif
				addSpan(hf, x, y, smin, smax, area, flagMergeThr);
			}

			// reset for next triangle
			hf.RowExt[y + 1].MinCol = hf.width + 2;
			hf.RowExt[y + 1].MaxCol = -2;
		}
	}
}