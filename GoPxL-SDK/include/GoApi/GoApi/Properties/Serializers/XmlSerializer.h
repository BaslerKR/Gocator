/**
* @file     XmlSerializer.h
* @brief    Declares the XmlSerializer class.
*/

#pragma once

#include <GoApi/Properties/Nodes.h>
#include <kApi/Data/kXml.h>

namespace Go
{
namespace Properties
{

/**
* @class   XmlSerializer
*/
class GoApiClass XmlSerializer
{
public:
    void Serialize(const Node& node, kXml xml, kXmlItem root);
    void Deserialize(Node& node, kXml xml, kXmlItem root);

    void SerializeSchema(const Node& node, kXml xml, kXmlItem root);
};

}
} //Namespaces

