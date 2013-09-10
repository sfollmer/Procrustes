#ifndef PRIMITIVES_H_
#define PRIMITIVES_H_

//#include <vector>
//#include <string>

//#include "module.h"
//#include "node.h"
//#include "polyset.h"
#include "visitor.h"


#define F_MINIMUM 0.01

enum primitive_type_e {
	CUBE,
	SPHERE,
	CYLINDER,
	POLYHEDRON,
	SQUARE,
	CIRCLE,
	POLYGON
};

class PrimitiveModule : public AbstractModule
{
public:
	primitive_type_e type;
	PrimitiveModule(primitive_type_e type) : type(type) { }
	virtual AbstractNode *instantiate(const Context *ctx, const ModuleInstantiation *inst, const EvalContext *evalctx) const;
};

class PrimitiveNode : public AbstractPolyNode
{
public:
	PrimitiveNode(const ModuleInstantiation *mi, primitive_type_e type) : AbstractPolyNode(mi), type(type) { }
  virtual Response accept(class State &state, Visitor &visitor) const {
		return visitor.visit(state, *this);
	}
	virtual std::string toString() const;
	virtual std::string name() const {
		switch (this->type) {
		case CUBE:
			return "cube";
			break;
		case SPHERE:
			return "sphere";
			break;
		case CYLINDER:
			return "cylinder";
			break;
		case POLYHEDRON:
			return "polyhedron";
			break;
		case SQUARE:
			return "square";
			break;
		case CIRCLE:
			return "circle";
			break;
		case POLYGON:
			return "polygon";
			break;
		default:
			assert(false && "PrimitiveNode::name(): Unknown primitive type");
			return AbstractPolyNode::name();
		}
	}

	bool center;
	double x, y, z, h, r1, r2;
	double fn, fs, fa;
	primitive_type_e type;
	int convexity;
	Value points, paths, triangles;
	virtual PolySet *evaluate_polyset(class PolySetEvaluator *) const;
};

#endif
