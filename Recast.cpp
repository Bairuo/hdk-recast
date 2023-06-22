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

#include "Recast.h"
#include "RecastAlloc.h"
#include "RecastMath.h"
#include <cstring>

void rcFreeHeightField(rcHeightfield* hf)
{
    if (!hf) return;
    // Delete span array.
    rcFree(hf->spans);
    // Delete span pools.
    while (hf->pools)
    {
        rcSpanPool* next = hf->pools->next;
        rcFree(hf->pools);
        hf->pools = next;
    }

    rcFree(hf->EdgeHits);
    rcFree(hf->RowExt);
    rcFree(hf->tempspans);
    
    rcFree(hf);
}

rcHeightfield* rcAllocHeightfield()
{
    rcHeightfield* hf = (rcHeightfield*)rcAlloc(sizeof(rcHeightfield), RC_ALLOC_PERM);
    memset(hf, 0, sizeof(rcHeightfield));
    return hf;
}

bool rcCreateHeightfield(rcHeightfield& hf, int width, int height,
                         const float* bmin, const float* bmax,
                         float cs, float ch)
{
    // TODO: VC complains about unref formal variable, figure out a way to handle this better.
    //	rcAssert(ctx);
	
    hf.width = width;
    hf.height = height;
    rcVcopy(hf.bmin, bmin);
    rcVcopy(hf.bmax, bmax);
    hf.cs = cs;
    hf.ch = ch;
    hf.spans = (rcSpan**)rcAlloc(sizeof(rcSpan*)*hf.width*hf.height, RC_ALLOC_PERM);
    if (!hf.spans)
        return false;
    memset(hf.spans, 0, sizeof(rcSpan*)*hf.width*hf.height);
    
    hf.EdgeHits = (rcEdgeHit*)rcAlloc(sizeof(rcEdgeHit) * (hf.height + 1), RC_ALLOC_PERM); 
    if (!hf.EdgeHits)
        return false;
    memset(hf.EdgeHits, 0, sizeof(rcEdgeHit) * (hf.height + 1));

    hf.RowExt = (rcRowExt*)rcAlloc(sizeof(rcRowExt) * (hf.height + 2), RC_ALLOC_PERM); 

    for (int i = 0; i < hf.height + 2; i++)
    {
        hf.RowExt[i].MinCol = hf.width + 2;
        hf.RowExt[i].MaxCol = -2;
    }

    hf.tempspans = (rcTempSpan*)rcAlloc(sizeof(rcTempSpan)*(hf.width + 2) * (hf.height + 2), RC_ALLOC_PERM); 
    if (!hf.tempspans)
        return false;

    for (int i = 0; i < hf.height + 2; i++)
    {
        for (int j = 0; j < hf.width + 2; j++)
        {
            hf.tempspans[i * (hf.width + 2) + j].sminmax[0] = 32000;
            hf.tempspans[i * (hf.width + 2) + j].sminmax[1] = -32000;
        }
    }

    return true;
}