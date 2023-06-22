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

#include "RecastAlloc.h"
#include <cstdlib>
#include <cstring>

static void *rcAllocDefault(int size, rcAllocHint)
{
	return malloc(size);
}

static void rcFreeDefault(void *ptr)
{
	free(ptr);
}

void rcMemCpy(void* dst, void* src, int size)
{
	memcpy(dst, src, size);
}

static rcAllocFunc* sRecastAllocFunc = rcAllocDefault;
static rcFreeFunc* sRecastFreeFunc = rcFreeDefault;

/// @see rcAlloc, rcFree
void rcAllocSetCustom(rcAllocFunc *allocFunc, rcFreeFunc *freeFunc)
{
	sRecastAllocFunc = allocFunc ? allocFunc : rcAllocDefault;
	sRecastFreeFunc = freeFunc ? freeFunc : rcFreeDefault;
}

/// @see rcAllocSetCustom
void* rcAlloc(int size, rcAllocHint hint)
{
	return sRecastAllocFunc(size, hint);
}

/// @par
///
/// @warning This function leaves the value of @p ptr unchanged.  So it still
/// points to the same (now invalid) location, and not to null.
/// 
/// @see rcAllocSetCustom
void rcFree(void* ptr)
{
	if (ptr)
		sRecastFreeFunc(ptr);
}

/// @class rcIntArray
///
/// While it is possible to pre-allocate a specific array size during 
/// construction or by using the #resize method, certain methods will 
/// automatically resize the array as needed.
///
/// @warning The array memory is not initialized to zero when the size is 
/// manually set during construction or when using #resize.

/// @par
///
/// Using this method ensures the array is at least large enough to hold
/// the specified number of elements.  This can improve performance by
/// avoiding auto-resizing during use.
void rcIntArray::resize(int n)
{
	if (n > m_cap)
	{
		if (!m_cap) m_cap = n;
		while (m_cap < n) m_cap *= 2;
		int* newData = (int*)rcAlloc(m_cap*sizeof(int), RC_ALLOC_TEMP);
		if (m_size && newData) memcpy(newData, m_data, m_size*sizeof(int));
		rcFree(m_data);
		m_data = newData;
	}
	m_size = n;
}

// @UE4 BEGIN: added contains()
bool rcIntArray::contains(int v) const
{
	for (int i = 0; i < m_size; i++)
	{
		if (m_data[i] == v)
			return true;
	}

	return false;
}
// @UE4 END
