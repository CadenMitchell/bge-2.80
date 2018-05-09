/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): Tristan Porteries.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file RAS_FrameBuffer.h
 *  \ingroup bgerast
 */

#ifndef __RAS_FRAMEBUFFER_H__
#define __RAS_FRAMEBUFFER_H__

#include "RAS_Rasterizer.h"

struct GPUFrameBuffer;
struct GPUTexture;

class RAS_FrameBuffer
{
private:
	/// All the off screens used.
	GPUFrameBuffer *m_frameBuffer;
	/// The off screen type, render, final, filter ect...
	RAS_Rasterizer::FrameBufferType m_frameBufferType;
	RAS_Rasterizer::HdrType m_hdrType;

public:
	RAS_FrameBuffer(unsigned int width, unsigned height, RAS_Rasterizer::HdrType hdrType, RAS_Rasterizer::FrameBufferType framebufferType);
	~RAS_FrameBuffer();

	GPUFrameBuffer *GetFrameBuffer();
	/// NOTE: This function has the side effect to leave the destination off screen bound.
	RAS_FrameBuffer *Blit(RAS_FrameBuffer *dstFrameBuffer, bool color, bool depth);

	unsigned GetWidth() const;
	unsigned GetHeight() const;

	GPUTexture *m_colorAttachment;
	GPUTexture *m_depthAttachment;

	RAS_Rasterizer::FrameBufferType GetType() const;
};

#endif  // __RAS_FRAMEBUFFER_H__
