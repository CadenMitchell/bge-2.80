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

/** \file RAS_BoundingBox.cpp
 *  \ingroup bgerast
 */

#include "RAS_BoundingBox.h"
#include "RAS_BoundingBoxManager.h"
#include <algorithm>

/* EEVEE INTEGRATION */
extern "C" {
#  include "BKE_object.h"
#  include "DNA_object_types.h"
}
/* End of EEVEE INTEGRATION */

RAS_BoundingBox::RAS_BoundingBox(RAS_BoundingBoxManager *manager)
	:m_modified(false),
	m_aabbMin(0.0f, 0.0f, 0.0f),
	m_aabbMax(0.0f, 0.0f, 0.0f),
	m_users(0),
	m_manager(manager)
{
	BLI_assert(m_manager);
	m_manager->m_boundingBoxList.push_back(this);
}

RAS_BoundingBox::~RAS_BoundingBox()
{
}

RAS_BoundingBox *RAS_BoundingBox::GetReplica()
{
	RAS_BoundingBox *boundingBox = new RAS_BoundingBox(*this);
	boundingBox->ProcessReplica();
	return boundingBox;
}

void RAS_BoundingBox::ProcessReplica()
{
	m_users = 1;
	m_manager->m_boundingBoxList.push_back(this);
}

void RAS_BoundingBox::AddUser()
{
	++m_users;
	/* No one was using this bounding box previously. Then add it to the active
	 * bounding box list in the manager.*/
	if (m_users == 1) {
		m_manager->m_activeBoundingBoxList.push_back(this);
	}
}

void RAS_BoundingBox::RemoveUser()
{
	--m_users;
	BLI_assert(m_users >= 0);

	/* Some one was using this bounding box previously. Then remove it from the
	 * active bounding box list. */
	if (m_users == 0) {
		RAS_BoundingBoxList::const_iterator it = std::find(m_manager->m_activeBoundingBoxList.begin(),
														   m_manager->m_activeBoundingBoxList.end(), this);
		m_manager->m_activeBoundingBoxList.erase(it);
	}
}

void RAS_BoundingBox::SetManager(RAS_BoundingBoxManager *manager)
{
	m_manager = manager;
}

bool RAS_BoundingBox::GetModified() const
{
	return m_modified;
}

void RAS_BoundingBox::ClearModified()
{
	m_modified = false;
}

void RAS_BoundingBox::GetAabb(MT_Vector3& aabbMin, MT_Vector3& aabbMax) const
{
	aabbMin = m_aabbMin;
	aabbMax = m_aabbMax;
}

void RAS_BoundingBox::SetAabb(const MT_Vector3& aabbMin, const MT_Vector3& aabbMax)
{
	m_aabbMin = aabbMin;
	m_aabbMax = aabbMax;
	m_modified = true;
}

void RAS_BoundingBox::ExtendAabb(const MT_Vector3& aabbMin, const MT_Vector3& aabbMax)
{
	m_aabbMin.x() = std::min(m_aabbMin.x(), aabbMin.x());
	m_aabbMin.y() = std::min(m_aabbMin.y(), aabbMin.y());
	m_aabbMin.z() = std::min(m_aabbMin.z(), aabbMin.z());
	m_aabbMax.x() = std::max(m_aabbMax.x(), aabbMax.x());
	m_aabbMax.y() = std::max(m_aabbMax.y(), aabbMax.y());
	m_aabbMax.z() = std::max(m_aabbMax.z(), aabbMax.z());
	m_modified = true;
}

void RAS_BoundingBox::CopyAabb(RAS_BoundingBox *other)
{
	other->GetAabb(m_aabbMin, m_aabbMax);
	m_modified = true;
}

void RAS_BoundingBox::Update(bool force)
{
}

RAS_BoundingBoxFromObject::RAS_BoundingBoxFromObject(RAS_BoundingBoxManager *manager, Object *ob)
	:RAS_BoundingBox(manager),
	m_ob(ob)
{
}

RAS_BoundingBoxFromObject::~RAS_BoundingBoxFromObject()
{
}

RAS_BoundingBox *RAS_BoundingBoxFromObject::GetReplica()
{
	RAS_BoundingBoxFromObject *boundingBox = new RAS_BoundingBoxFromObject(*this);
	boundingBox->m_users = 0;
	return boundingBox;
}

void RAS_BoundingBoxFromObject::Update(bool force)
{
	/* EEVEE INTEGRATION: Don't update in realtime for now */
	if (!m_ob || !ELEM(m_ob->type, OB_MESH, OB_CURVE)) {
		return;
	}

	if (force) {
		BoundBox *bbox = BKE_object_boundbox_get(m_ob);

		float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;

		for (int i = 0; i < 8; i++) {
			float x = bbox->vec[i][0];
			float y = bbox->vec[i][1];
			float z = bbox->vec[i][2];

			if (x < minX) minX = x;
			if (y < minY) minY = y;
			if (z < minZ) minZ = z;
			if (x > maxX) maxX = x;
			if (y > maxY) maxY = y;
			if (z > maxZ) maxZ = z;
		}

		m_aabbMin.x() = minX;
		m_aabbMin.y() = minY;
		m_aabbMin.z() = minZ;
		m_aabbMax.x() = maxX;
		m_aabbMax.y() = maxY;
		m_aabbMax.z() = maxZ;

		m_modified = true;
	}
}
