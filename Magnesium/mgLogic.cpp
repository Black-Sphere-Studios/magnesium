// Copyright �2017 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in Magnesium.h

#include "mgLogic.h"

using namespace magnesium;

LogicSystem::LogicSystem() {}
LogicSystem::~LogicSystem() {}

void LogicSystem::Process()
{
  mgSystemSimple::Iterate<&LogicSystem::Iterator>(this);
}

void LogicSystem::Iterator(mgSystemBase*, mgEntity& entity)
{
  auto& f = entity.Get<mgLogicComponent>()->onlogic;
  if (f) f(entity);
}
