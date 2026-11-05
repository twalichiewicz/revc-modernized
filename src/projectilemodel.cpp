#include "projectilemodel.h"
namespace projectilemodel {
Result Doses(const mechanicalmodel::Layout &layout,const Ray &ray){return materialmodel::Trace(layout,ray);}
}
