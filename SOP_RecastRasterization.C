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

#include "SOP_RecastRasterization.h"
#include "SOP_RecastRasterization.proto.h"

#include <GU/GU_Detail.h>
#include <GU/GU_PrimPoly.h>
#include <GEO/GEO_PrimPoly.h>
#include <OP/OP_Operator.h>
#include <OP/OP_OperatorTable.h>
#include <PRM/PRM_Include.h>
#include <PRM/PRM_TemplateBuilder.h>
#include <UT/UT_DSOVersion.h>
#include <UT/UT_Interrupt.h>
#include <UT/UT_StringHolder.h>
#include <OP/OP_AutoLockInputs.h>
#include <SYS/SYS_Math.h>
#include <limits.h>

#include "Recast.h"

using namespace HDK_Recast;


const UT_StringHolder SOP_RecastRasterization::theSOPTypeName("RecastRasterization"_sh);

/// newSopOperator is the hook that Houdini grabs from this dll
/// and invokes to register the SOP.  In this case, we add ourselves
/// to the specified operator table.
void
newSopOperator(OP_OperatorTable *table)
{
    table->addOperator(new OP_Operator(
        SOP_RecastRasterization::theSOPTypeName,   // Internal name
        "RecastRasterization",                     // UI name
        SOP_RecastRasterization::myConstructor,    // How to build the SOP
        SOP_RecastRasterization::buildTemplates(), // My parameters
        1,                          // Min # of sources
        1,                          // Max # of sources
        nullptr,                    // Custom local variables (none)
        OP_FLAG_GENERATOR));        // Flag it as generator
}

/// This is a multi-line raw string specifying the parameter interface
/// for this SOP.
static const char *theDsFile = R"THEDSFILE(
{
    name        parameters
    parm {
        name    "cs"      // Internal parameter name
        label   "Cell Size" // Descriptive parameter name for user interface
        type    float
        default { "0.19" }     // Default for this parameter on new nodes
        range   { 0.01! 10 }   // The value is prevented from going below 2 at all.
                            // The UI slider goes up to 50, but the value can go higher.
        export  all         // This makes the parameter show up in the toolbox
                            // above the viewport when it's in the node's state.
    }
    parm {
        name    "ch"      // Internal parameter name
        label   "Cell Height" // Descriptive parameter name for user interface
        type    float
        default { "0.1" }     // Default for this parameter on new nodes
        range   { 0.01! 10 }   // The value is prevented from going below 2 at all.
                            // The UI slider goes up to 50, but the value can go higher.
        export  all         // This makes the parameter show up in the toolbox
                            // above the viewport when it's in the node's state.
    }
    parm {
        name    "mode"
        label   "mode"
        type    ordinal
        default { "1" }
        menu {
            "span"     "Recast Span Heightfield"
            "voxelization"     "Voxelization"
            "sppoints"    "Span Points"
            "voxpoints"    "Voxelization Points"
        }
    }
    parm {
        name    "wireframe"
        label   "Wireframe(Open box poly)"
        type    toggle
        default { "0" }
    }
}
)THEDSFILE";

PRM_Template*
SOP_RecastRasterization::buildTemplates()
{
    static PRM_TemplateBuilder templ("SOP_RecastRasterization.C"_sh, theDsFile);
    return templ.templates();
}

void SOP_RecastRasterization::addBox(const UT_Vector3& vmin, const UT_Vector3& vmax)
{
    UT_Vector3 pos[8] = {
        UT_Vector3(vmin.x(), vmin.y(), vmin.z()),
        UT_Vector3(vmin.x(), vmin.y(), vmax.z()),
        UT_Vector3(vmin.x(), vmax.y(), vmax.z()),
        UT_Vector3(vmin.x(), vmax.y(), vmin.z()),
        UT_Vector3(vmax.x(), vmin.y(), vmin.z()),
        UT_Vector3(vmax.x(), vmin.y(), vmax.z()),
        UT_Vector3(vmax.x(), vmax.y(), vmax.z()),
        UT_Vector3(vmax.x(), vmax.y(), vmin.z())
    };

    static int pts[36] = {
        0, 2, 1,
        0, 3, 2,
        4, 6, 5,
        4, 7, 6,
        0, 5, 4,
        0, 1, 5,
        1, 6, 5,
        1, 2, 6,
        2, 7, 6,
        2, 3, 7,
        3, 4, 7,
        3, 0, 4
    };

    GA_Offset v[8];
    for(int i = 0; i < 8; i++)
    {
        v[i] = gdp->appendPointOffset();
        gdp->setPos3(v[i], pos[i]);
    }

    bool bOpen = evalInt("wireframe", 0, 0);

    for (int i = 0; i < 12; i++) 
    {
        int a = pts[i*3];
        int b = pts[i*3+1];
        int c = pts[i*3+2];

        GEO_PrimPoly* poly = GEO_PrimPoly::build(gdp, 3, bOpen);

        poly->setVertexPoint(0, v[a]);
        poly->setVertexPoint(1, v[b]);
        poly->setVertexPoint(2, v[c]);
    }
}

OP_ERROR SOP_RecastRasterization::cookMySop(OP_Context& context)
{
    OP_AutoLockInputs inputs(this);
    if (inputs.lock(context) >= UT_ERROR_ABORT)
        return error();

    gdp->clearAndDestroy();

    const GU_Detail* input_gdp = inputGeo(0);

    if(input_gdp == nullptr)
    {
        return error();
    }
    
    UT_BoundingBox bbox;
    input_gdp->getCachedBounds(bbox);
    
    UT_Vector3 bound_off(10, 10, 10);
    UT_Vector3 min_pos = bbox.minvec() - bound_off;
    UT_Vector3 max_pos = bbox.maxvec() + bound_off;
    
    rcHeightfield* Solid = rcAllocHeightfield();
    if(Solid == nullptr)
    {
        return error();
    }

    UT_Vector3 SizeBox = max_pos - min_pos;

    float cs = evalFloat("cs", 0, 0);
    float ch = evalFloat("ch", 0, 0);

    if(cs <= 0.01f || ch <= 0.01f)
    {
        return error();
    }
    
    const int width = SizeBox.x() / cs;
    const int height = SizeBox.z() / ch;
    
    if (!rcCreateHeightfield(*Solid, width, height, min_pos.vec, max_pos.vec, cs, ch))
    {
        return error();
    }
    
    const float ics = 1.0f / Solid->cs;
    const float ich = 1.0f / Solid->ch;
    
    for (GA_Iterator it(input_gdp->getPrimitiveRange()); !it.atEnd(); it.advance())
    {
        const GEO_Primitive* prim = input_gdp->getGEOPrimitive(it.getOffset());
        if (prim->getTypeId() != GA_PRIMPOLY || prim->getVertexCount() != 3)
            continue;

        const float *v0 = prim->getPos3(0).vec;
        const float *v1 = prim->getPos3(1).vec;
        const float *v2 = prim->getPos3(2).vec;

        rasterizeTri(v0, v1, v2, RC_WALKABLE_AREA, *Solid, Solid->bmin, Solid->bmax, Solid->cs, ics, ich, 4, 0, NULL);
    }

    int mode = evalInt("mode", 0, 0);
    
    for(int x = 0; x < Solid->width; x++)
    {
        for(int y = 0; y < Solid->height; y++)
        {
            int idx = x + y * Solid->width;
    
            rcSpan* cur = Solid->spans[idx];
    
            while(cur)
            {
                switch (mode)
                {
                case 0:     // Recast Span Heightfield
                    {
                        UT_Vector3 vmin{
                            x * cs + min_pos.x(),
                            cur->data.smin * ch + min_pos.y(),
                            y * cs + min_pos.z()
                        };

                        UT_Vector3 vmax{
                            x * cs + min_pos.x() + cs,
                            cur->data.smax * ch + min_pos.y(),
                            y * cs + min_pos.z() + cs
                        };
                    
                        addBox(vmin, vmax);
                    }
                    break;
                case 1:     // Voxelization
                    {
                        for(int z = cur->data.smin; z < cur->data.smax; z++)
                        {
                            UT_Vector3 vmin{
                                x * cs + min_pos.x(),
                                z * ch + min_pos.y(),
                                y * cs + min_pos.z()
                            };

                            UT_Vector3 vmax{
                                x * cs + min_pos.x() + cs,
                                z * ch + min_pos.y() + ch,
                                y * cs + min_pos.z() + cs
                            };

                            addBox(vmin, vmax);
                        }
                    }
                    break;
                case 2:     // Span Points
                    {
                        float smin = cur->data.smin * ch + min_pos.y();
                        float smax = cur->data.smax * ch + min_pos.y();
                    
                        UT_Vector3 center{
                            x * cs + min_pos.x() + cs / 2,
                            cur->data.smax * ch + min_pos.y(),
                            y * cs + min_pos.z() + cs / 2
                        };
    
                        GA_Offset ptoff = gdp->appendPointOffset();
                        gdp->setPos3(ptoff, center);

                        GA_Attribute* spanMin_attrib = gdp->addFloatTuple(GA_ATTRIB_POINT, "spanMin", 1, GA_Defaults(0));
                        GA_RWHandleF  spanMin(spanMin_attrib);
                        spanMin.set(ptoff, smin);

                        GA_Attribute* spanMax_attrib = gdp->addFloatTuple(GA_ATTRIB_POINT, "spanMax", 1, GA_Defaults(0));
                        GA_RWHandleF  spanMax(spanMax_attrib);
                        spanMax.set(ptoff, smax);
                    }
                    break;
                case 3:     // Voxelization Points
                    {
                        float smin = cur->data.smin * ch + min_pos.y();
                        float smax = cur->data.smax * ch + min_pos.y();
                    
                        for(int z = cur->data.smin; z < cur->data.smax; z++)
                        {
                            UT_Vector3 center{
                                x * cs + min_pos.x() + cs / 2,
                                z * ch + min_pos.y() + ch / 2,
                                y * cs + min_pos.z() + cs / 2
                            };
    
                            GA_Offset ptoff = gdp->appendPointOffset();
                            gdp->setPos3(ptoff, center);

                            GA_Attribute* spanMin_attrib = gdp->addFloatTuple(GA_ATTRIB_POINT, "spanMin", 1, GA_Defaults(0));
                            GA_RWHandleF  spanMin(spanMin_attrib);
                            spanMin.set(ptoff, smin);

                            GA_Attribute* spanMax_attrib = gdp->addFloatTuple(GA_ATTRIB_POINT, "spanMax", 1, GA_Defaults(0));
                            GA_RWHandleF  spanMax(spanMax_attrib);
                            spanMax.set(ptoff, smax);
                        } 
                    }
                default:
                    break;
                }
                
                cur = cur->next;
            }
        }
    }

    rcFreeHeightField(Solid);
    
    return error();
}