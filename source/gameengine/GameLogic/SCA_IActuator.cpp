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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file gameengine/GameLogic/SCA_IActuator.cpp
 *  \ingroup gamelogic
 */


#include "SCA_IActuator.h"
#include "CM_Message.h"

#include <algorithm>

SCA_IActuator::SCA_IActuator(SCA_IObject *gameobj, KX_ACTUATOR_TYPE type) :
	SCA_ILogicBrick(gameobj),
	m_type(type),
	m_links(0),
	m_posevent(false),
	m_negevent(false)
{
}

void SCA_IActuator::RemoveAllEvents()
{
	m_posevent = false;
	m_negevent = false;
}

bool SCA_IActuator::UnlinkObject(SCA_IObject *clientobj)
{
	return false;
}

bool SCA_IActuator::Update(double curtime)
{
	return Update();
}

bool SCA_IActuator::Update()
{
	BLI_assert(false && "Actuators should override an Update method.");
	return false;
}

void SCA_IActuator::AddEvent(bool event)
{
	if (event) {
		m_posevent = true;
	}
	else {
		m_negevent = true;
	}
}

bool SCA_IActuator::IsNegativeEvent() const
{
	return !m_posevent && m_negevent;
}

bool SCA_IActuator::IsPositiveEvent() const
{
	return m_posevent && !m_negevent;
}

void SCA_IActuator::Activate(SG_DList& head)
{
	if (QEmpty()) {
		InsertActiveQList(m_gameobj->m_activeActuators);
		head.AddBack(&m_gameobj->m_activeActuators);
	}
}

void SCA_IActuator::Deactivate()
{
	if (QDelink()) {
		// the actuator was in the active list
		if (m_gameobj->m_activeActuators.QEmpty()) {
			// the owner object has no more active actuators, remove it from the global list
			m_gameobj->m_activeActuators.Delink();
		}
	}
}

void SCA_IActuator::ProcessReplica()
{
	SCA_ILogicBrick::ProcessReplica();
	RemoveAllEvents();
	m_linkedcontrollers.clear();
}

SCA_IActuator::~SCA_IActuator()
{
	RemoveAllEvents();
}

void SCA_IActuator::ClrLink()
{
	m_links = 0;
}
void SCA_IActuator::IncLink()
{
	m_links++;
}

void SCA_IActuator::DecLink()
{
	--m_links;
	if (m_links < 0) {
		CM_LogicBrickWarning(this, "actuator " << m_name << " has negative m_links: " << m_links);
		m_links = 0;
	}
}

bool SCA_IActuator::IsNoLink() const
{
	return !m_links;
}
bool SCA_IActuator::IsType(KX_ACTUATOR_TYPE type)
{
	return m_type == type;
}

void SCA_IActuator::LinkToController(SCA_IController *controller)
{
	m_linkedcontrollers.push_back(controller);
}

void SCA_IActuator::UnlinkController(SCA_IController *controller)
{
	std::vector<SCA_IController *>::iterator it = std::find(m_linkedcontrollers.begin(), m_linkedcontrollers.end(), controller);
	if (it != m_linkedcontrollers.end()) {
		m_linkedcontrollers.erase(it);
	}
	else {
		CM_LogicBrickWarning(this, "Missing link from actuator " << m_gameobj->GetName() << ":"
			<< GetName() << " to controller " << controller->GetParent()->GetName() << ":" << controller->GetName());
	}
}

void SCA_IActuator::UnlinkAllControllers()
{
	for (SCA_IController *controller : m_linkedcontrollers) {
		controller->UnlinkActuator(this);
	}
	m_linkedcontrollers.clear();
}
