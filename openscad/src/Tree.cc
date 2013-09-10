#include "Tree.h"
#include "nodedumper.h"

#include <assert.h>
#include <algorithm>

Tree::~Tree()
{
	this->nodecache.clear();
	this->nodeidcache.clear();
}

/*!
	Returns the cached string representation of the subtree rooted by \a node.
	If node is not cached, the cache will be rebuilt.
*/
const std::string &Tree::getString(const AbstractNode &node) const
{
	assert(this->root_node);
	if (!this->nodecache.contains(node)) {
		this->nodecache.clear();
		this->nodeidcache.clear();
		NodeDumper dumper(this->nodecache, false);
		Traverser trav(dumper, *this->root_node, Traverser::PRE_AND_POSTFIX);
		trav.execute();
		assert(this->nodecache.contains(*this->root_node) &&
					 "NodeDumper failed to create a cache");
	}
	return this->nodecache[node];
}

static bool filter(char c)
{
	return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}

/*!
	Returns the cached ID string representation of the subtree rooted by \a node.
	If node is not cached, the cache will be rebuilt.

	The difference between this method and getString() is that the ID string
	is stripped for whitespace. Especially indentation whitespace is important to
	strip to enable cache hits for equivalent nodes from different scopes.
*/
const std::string &Tree::getIdString(const AbstractNode &node) const
{
	assert(this->root_node);
	if (!this->nodeidcache.contains(node)) {
		std::string str = getString(node);
		str.erase(std::remove_if(str.begin(), str.end(), filter), str.end());
		return this->nodeidcache.insert(node, str);
	}
	return this->nodeidcache[node];
}

/*!
	Sets a new root. Will clear the existing cache.
 */
void Tree::setRoot(const AbstractNode *root)
{
	this->root_node = root; 
	this->nodecache.clear();
}
