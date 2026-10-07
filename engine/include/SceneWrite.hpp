#pragma once

#include "Object.hpp"
#include "Material.hpp"

#include <ostream>
#include <string>

namespace scene_write
{

std::string escapeName(const std::string &name);
void writeTag(std::ostream &out, const std::string &tag);
void writeMotion(std::ostream &out, const Object &object);
void writeAction(std::ostream &out, const Object &object);
void writeParent(std::ostream &out, const Object &object);
void writeObjectTail(std::ostream &out, const Object &object);
void writeMaterial(std::ostream &out, const Material &material);

}
