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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Copyright (c) 2007 The Zdeno Ash Miklas
 *
 * This source file is part of VideoTexture library
 *
 * Contributor(s):
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file gameengine/VideoTexture/ImageRender.cpp
 *  \ingroup bgevideotex
 */

// implementation

#include "EXP_PyObjectPlus.h"
#include <structmember.h>
#include <float.h>
#include <math.h>

#include "GPU_framebuffer.h"
#include "GPU_texture.h"

#include "GPU_glew.h"

#include "KX_Globals.h"
#include "DNA_scene_types.h"
#include "RAS_FrameBuffer.h"
#include "RAS_CameraData.h"
#include "RAS_MeshObject.h"
#include "RAS_Polygon.h"
#include "RAS_IVertex.h"
#include "BLI_math.h"

#include "ImageRender.h"
#include "ImageBase.h"
#include "Exception.h"
#include "Texture.h"

extern "C" {
#  include "DRW_render.h"
#  include "eevee_private.h"
}

ExceptionID SceneInvalid, CameraInvalid, ObserverInvalid, FrameBufferInvalid;
ExpDesc SceneInvalidDesc(SceneInvalid, "Scene object is invalid");
ExpDesc CameraInvalidDesc(CameraInvalid, "Camera object is invalid");
ExpDesc ObserverInvalidDesc(ObserverInvalid, "Observer object is invalid");
ExpDesc FrameBufferInvalidDesc(FrameBufferInvalid, "FrameBuffer object is invalid");

// constructor
ImageRender::ImageRender (KX_Scene *scene, KX_Camera * camera, unsigned int width, unsigned int height, unsigned short samples) :
    ImageViewport(width, height),
    m_render(true),
    m_done(false),
    m_scene(scene),
    m_camera(camera),
    m_owncamera(false)
{
	// retrieve rendering objects
	m_engine = KX_GetActiveEngine();
	m_rasterizer = m_engine->GetRasterizer();
	m_canvas = m_engine->GetCanvas();

	RAS_Rasterizer::HdrType hdr = m_canvas->GetHdrType();
	if (hdr == RAS_Rasterizer::RAS_HDR_HALF_FLOAT) {
		m_internalFormat = GL_RGBA16F_ARB;
	}
	else if (hdr == RAS_Rasterizer::RAS_HDR_FULL_FLOAT) {
		m_internalFormat = GL_RGBA32F_ARB;
	}
	else {
		m_internalFormat = GL_R11F_G11F_B10F;
	}
}

// destructor
ImageRender::~ImageRender (void)
{
	if (m_owncamera) {
		m_camera->Release();
	}
}

int ImageRender::GetColorBindCode() const
{
	return GPU_texture_opengl_bindcode(EEVEE_engine_data_get()->stl->effects->final_tx);
}

// capture image from viewport
void ImageRender::calcViewport (unsigned int texId, double ts, unsigned int format)
{
	// render the scene from the camera
	if (!m_done) {
		if (!Render()) {
			return;
		}
	}
	m_done = false;

	// get image from viewport (or FBO)
	ImageViewport::calcViewport(texId, ts, format);

	const RAS_Rect& viewport = m_canvas->GetViewportArea();
	m_rasterizer->SetViewport(viewport.GetLeft(), viewport.GetBottom(), viewport.GetWidth() + 1, viewport.GetHeight() + 1);
	m_rasterizer->SetScissor(viewport.GetLeft(), viewport.GetBottom(), viewport.GetWidth() + 1, viewport.GetHeight() + 1);

	GPU_framebuffer_restore();

	DRW_transform_to_display(EEVEE_engine_data_get()->stl->effects->final_tx);
}

bool ImageRender::Render()
{

	DRW_state_reset();

	RAS_FrameFrustum frustum;

	if (!m_render ||
        m_camera->GetViewport() ||        // camera must be inactive
        m_camera == m_scene->GetActiveCamera())
	{
		// no need to compute texture in non texture rendering
		return false;
	}

	m_rasterizer->BeginFrame(m_engine->GetClockTime());

	m_rasterizer->SetViewport(m_position[0], m_position[1], m_position[0] + m_capSize[0], m_position[1] + m_capSize[1]);
	m_rasterizer->SetScissor(m_position[0], m_position[1], m_position[0] + m_capSize[0], m_position[1] + m_capSize[1]);

	m_rasterizer->Clear(RAS_Rasterizer::RAS_DEPTH_BUFFER_BIT);

	m_rasterizer->SetAuxilaryClientInfo(m_scene);

	if (!m_camera->hasValidProjectionMatrix()) {
		float lens = m_camera->GetLens();
		float sensor_x = m_camera->GetSensorWidth();
		float sensor_y = m_camera->GetSensorHeight();
		float shift_x = m_camera->GetShiftHorizontal();
		float shift_y = m_camera->GetShiftVertical();
		bool orthographic = !m_camera->GetCameraData()->m_perspective;
		float nearfrust = m_camera->GetCameraNear();
		float farfrust = m_camera->GetCameraFar();
		float aspect_ratio = 1.0f;
		Scene *blenderScene = m_scene->GetBlenderScene();
		MT_Matrix4x4 projmat;

		// compute the aspect ratio from frame blender scene settings so that render to texture
		// works the same in Blender and in Blender player
		if (blenderScene->r.ysch != 0)
			aspect_ratio = float(blenderScene->r.xsch*blenderScene->r.xasp) / float(blenderScene->r.ysch*blenderScene->r.yasp);

		if (orthographic) {

			RAS_FramingManager::ComputeDefaultOrtho(
			            nearfrust,
			            farfrust,
			            m_camera->GetScale(),
			            aspect_ratio,
						m_camera->GetSensorFit(),
			            shift_x,
			            shift_y,
			            frustum
			            );

			projmat = m_rasterizer->GetOrthoMatrix(
			            frustum.x1, frustum.x2, frustum.y1, frustum.y2, frustum.camnear, frustum.camfar);
		}
		else {
			RAS_FramingManager::ComputeDefaultFrustum(
			            nearfrust,
			            farfrust,
			            lens,
			            sensor_x,
			            sensor_y,
			            RAS_SENSORFIT_AUTO,
			            shift_x,
			            shift_y,
			            aspect_ratio,
			            frustum);
			
			projmat = m_rasterizer->GetFrustumMatrix(
			            frustum.x1, frustum.x2, frustum.y1, frustum.y2, frustum.camnear, frustum.camfar);
		}
		m_camera->SetProjectionMatrix(projmat);
	}

	MT_Transform camtrans(m_camera->GetWorldToCamera());
	MT_Matrix4x4 viewmat(camtrans);
	
	m_rasterizer->SetMatrix(viewmat, m_camera->GetProjectionMatrix(), m_camera->NodeGetWorldPosition(), m_camera->NodeGetLocalScaling());
	m_camera->SetModelviewMatrix(viewmat);

	KX_CullingNodeList nodes;
	m_scene->CalculateVisibleMeshes(nodes, m_camera, 0);

	m_engine->UpdateAnimations(m_scene);

	m_scene->RenderBucketsNew(nodes, m_rasterizer);

	m_canvas->EndFrame();

	// remember that we have done render
	m_done = true;
	// the image is not available at this stage
	m_avail = false;
	return true;
}

void ImageRender::Unbind()
{
	GPU_framebuffer_restore();
}

// cast Image pointer to ImageRender
inline ImageRender * getImageRender (PyImage *self)
{ return static_cast<ImageRender*>(self->m_image); }


// python methods

// object initialization
static int ImageRender_init(PyObject *pySelf, PyObject *args, PyObject *kwds)
{
	// parameters - scene object
	PyObject *scene;
	// camera object
	PyObject *camera;

	RAS_ICanvas *canvas = KX_GetActiveEngine()->GetCanvas();
	int width = canvas->GetWidth();
	int height = canvas->GetHeight();
	int samples = 0;
	// parameter keywords
	static const char *kwlist[] = {"sceneObj", "cameraObj", "width", "height", "samples", nullptr};
	// get parameters
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "OO|iiii",
		const_cast<char**>(kwlist), &scene, &camera, &width, &height, &samples))
		return -1;
	try
	{
		// get scene pointer
		KX_Scene * scenePtr (nullptr);
		if (!PyObject_TypeCheck(scene, &KX_Scene::Type)) {
			THRWEXCP(SceneInvalid, S_OK);
		}
		else {
			scenePtr = static_cast<KX_Scene *>BGE_PROXY_REF(scene);
		}

		// get camera pointer
		KX_Camera * cameraPtr (nullptr);
		if (!ConvertPythonToCamera(scenePtr, camera, &cameraPtr, false, "")) {
			THRWEXCP(CameraInvalid, S_OK);
		}

		// get pointer to image structure
		PyImage *self = reinterpret_cast<PyImage*>(pySelf);
		// create source object
		if (self->m_image != nullptr) delete self->m_image;
		self->m_image = new ImageRender(scenePtr, cameraPtr, width, height, samples);
	}
	catch (Exception & exp)
	{
		exp.report();
		return -1;
	}
	// initialization succeded
	return 0;
}

static PyObject *ImageRender_refresh(PyImage *self, PyObject *args)
{
	ImageRender *imageRender = getImageRender(self);

	if (!imageRender) {
		PyErr_SetString(PyExc_TypeError, "Incomplete ImageRender() object");
		return nullptr;
	}
	if (PyArg_ParseTuple(args, "")) {
		// refresh called with no argument.
		// For other image objects it simply invalidates the image buffer
		// For ImageRender it triggers a render+sync
		// Note that this only makes sense when doing offscreen render on texture
		if (!imageRender->isDone()) {
			if (!imageRender->Render()) {
				Py_RETURN_FALSE;
			}
			// as we are not trying to read the pixels, just unbind
			imageRender->Unbind();
		}
		// wait until all render operations are completed
		// this will also finalize the texture
		Py_RETURN_TRUE;
	}
	else {
		// fallback on standard processing
		PyErr_Clear();
		return Image_refresh(self, args);
	}
}

// refresh image
static PyObject *ImageRender_render(PyImage *self)
{
	ImageRender *imageRender = getImageRender(self);

	if (!imageRender) {
		PyErr_SetString(PyExc_TypeError, "Incomplete ImageRender() object");
		return nullptr;
	}
	if (!imageRender->Render()) {
		Py_RETURN_FALSE;
	}
	// we are not reading the pixels now, unbind
	imageRender->Unbind();
	Py_RETURN_TRUE;
}

static PyObject *getColorBindCode(PyImage *self, void *closure)
{
	return PyLong_FromLong(getImageRender(self)->GetColorBindCode());
}

// methods structure
static PyMethodDef imageRenderMethods[] =
{ // methods from ImageBase class
	{"refresh", (PyCFunction)ImageRender_refresh, METH_VARARGS, "Refresh image - invalidate its current content after optionally transferring its content to a target buffer"},
	{"render", (PyCFunction)ImageRender_render, METH_NOARGS, "Render scene - run before refresh() to performs asynchronous render"},
	{nullptr}
};
// attributes structure
static PyGetSetDef imageRenderGetSets[] =
{
	// attribute from ImageViewport
	{(char*)"capsize", (getter)ImageViewport_getCaptureSize, (setter)ImageViewport_setCaptureSize, (char*)"size of render area", nullptr},
	{(char*)"alpha", (getter)ImageViewport_getAlpha, (setter)ImageViewport_setAlpha, (char*)"use alpha in texture", nullptr},
	{(char*)"whole", (getter)ImageViewport_getWhole, (setter)ImageViewport_setWhole, (char*)"use whole viewport to render", nullptr},
	// attributes from ImageBase class
	{(char*)"valid", (getter)Image_valid, nullptr, (char*)"bool to tell if an image is available", nullptr},
	{(char*)"image", (getter)Image_getImage, nullptr, (char*)"image data", nullptr},
	{(char*)"size", (getter)Image_getSize, nullptr, (char*)"image size", nullptr},
	{(char*)"scale", (getter)Image_getScale, (setter)Image_setScale, (char*)"fast scale of image (near neighbor)",	nullptr},
	{(char*)"flip", (getter)Image_getFlip, (setter)Image_setFlip, (char*)"flip image vertically", nullptr},
	{(char*)"zbuff", (getter)Image_getZbuff, (setter)Image_setZbuff, (char*)"use depth buffer as texture", nullptr},
	{(char*)"depth", (getter)Image_getDepth, (setter)Image_setDepth, (char*)"get depth information from z-buffer using unsigned int precision", nullptr},
	{(char*)"filter", (getter)Image_getFilter, (setter)Image_setFilter, (char*)"pixel filter", nullptr},
	{(char*)"colorBindCode", (getter)getColorBindCode, nullptr, (char*)"Off-screen color texture bind code", nullptr},
	{nullptr}
};


// define python type
PyTypeObject ImageRenderType = {
	PyVarObject_HEAD_INIT(nullptr, 0)
	"VideoTexture.ImageRender",   /*tp_name*/
	sizeof(PyImage),          /*tp_basicsize*/
	0,                         /*tp_itemsize*/
	(destructor)Image_dealloc, /*tp_dealloc*/
	0,                         /*tp_print*/
	0,                         /*tp_getattr*/
	0,                         /*tp_setattr*/
	0,                         /*tp_compare*/
	0,                         /*tp_repr*/
	0,                         /*tp_as_number*/
	0,                         /*tp_as_sequence*/
	0,                         /*tp_as_mapping*/
	0,                         /*tp_hash */
	0,                         /*tp_call*/
	0,                         /*tp_str*/
	0,                         /*tp_getattro*/
	0,                         /*tp_setattro*/
	&imageBufferProcs,         /*tp_as_buffer*/
	Py_TPFLAGS_DEFAULT,        /*tp_flags*/
	"Image source from render",       /* tp_doc */
	0,		               /* tp_traverse */
	0,		               /* tp_clear */
	0,		               /* tp_richcompare */
	0,		               /* tp_weaklistoffset */
	0,		               /* tp_iter */
	0,		               /* tp_iternext */
	imageRenderMethods,    /* tp_methods */
	0,                   /* tp_members */
	imageRenderGetSets,          /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	0,                         /* tp_dictoffset */
	(initproc)ImageRender_init,     /* tp_init */
	0,                         /* tp_alloc */
	Image_allocNew,           /* tp_new */
};