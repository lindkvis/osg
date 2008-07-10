/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield 
 *
 * This library is open source and may be redistributed and/or modified under  
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or 
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * OpenSceneGraph Public License for more details.
*/

#include <osg/KdTree>
#include <osg/Geode>
#include <osg/TriangleIndexFunctor>
#include <osg/Timer>

#include <osg/io_utils>

using namespace osg;

//#define VERBOSE_OUTPUT

////////////////////////////////////////////////////////////////////////////////
//
// Functor for collecting triangle indices from Geometry

struct TriangleIndicesCollector
{
    TriangleIndicesCollector():
        _kdTree(0)
    {
    }

    inline void operator () (unsigned int p1, unsigned int p2, unsigned int p3)
    {
        unsigned int i = _kdTree->_triangles.size();
        _kdTree->_triangles.push_back(KdTree::Triangle(p1,p2,p3));
        
        osg::BoundingBox bb;
        bb.expandBy((*(_kdTree->_vertices))[p1]);
        bb.expandBy((*(_kdTree->_vertices))[p2]);
        bb.expandBy((*(_kdTree->_vertices))[p3]);

        _kdTree->_centers.push_back(bb.center());
        _kdTree->_primitiveIndices.push_back(i);
        
    }
    
    KdTree* _kdTree;

};

////////////////////////////////////////////////////////////////////////////////
//
// KdTree

KdTree::BuildOptions::BuildOptions():
        _numVerticesProcessed(0),
        _targetNumTrianglesPerLeaf(2),
        _maxNumLevels(32)
{
}

////////////////////////////////////////////////////////////////////////////////
//
// KdTree

KdTree::KdTree()
{
}

KdTree::KdTree(const KdTree& rhs, const osg::CopyOp& copyop):
    Shape(rhs)
{
}

bool KdTree::build(BuildOptions& options, osg::Geometry* geometry)
{
#ifdef VERBOSE_OUTPUT    
    osg::notify(osg::NOTICE)<<"osg::KDTreeBuilder::createKDTree()"<<std::endl;
#endif

    osg::Vec3Array* vertices = dynamic_cast<osg::Vec3Array*>(geometry->getVertexArray());
    if (!vertices) return false;
    
    if (vertices->size() <= options._targetNumTrianglesPerLeaf) return false;

    _geometry = geometry;
    _bb = _geometry->getBound();
    _vertices = vertices;
    
    unsigned int estimatedSize = (unsigned int)(2.0*float(vertices->size())/float(options._targetNumTrianglesPerLeaf));

#ifdef VERBOSE_OUTPUT    
    osg::notify(osg::NOTICE)<<"kdTree->_kdNodes.reserve()="<<estimatedSize<<std::endl<<std::endl;
#endif

    _kdNodes.reserve(estimatedSize*5);
    
    computeDivisions(options);

    options._numVerticesProcessed += vertices->size();

    unsigned int estimatedNumTriangles = vertices->size()*2;
    _primitiveIndices.reserve(estimatedNumTriangles);
    _triangles.reserve(estimatedNumTriangles);
    _centers.reserve(estimatedNumTriangles);



    osg::TriangleIndexFunctor<TriangleIndicesCollector> collectTriangleIndices;
    collectTriangleIndices._kdTree = this;
    geometry->accept(collectTriangleIndices);

    _primitiveIndices.reserve(vertices->size());

    KdNode node(-1, _primitiveIndices.size());
    node.bb = _bb;

    int nodeNum = addNode(node);

    osg::BoundingBox bb = _bb;
    nodeNum = divide(options, bb, nodeNum, 0);
    
#ifdef VERBOSE_OUTPUT    
    osg::notify(osg::NOTICE)<<"Root nodeNum="<<nodeNum<<std::endl;
#endif
    
    
//    osg::notify(osg::NOTICE)<<"_kdNodes.size()="<<k_kdNodes.size()<<"  estimated size = "<<estimatedSize<<std::endl;
//    osg::notify(osg::NOTICE)<<"_kdLeaves.size()="<<_kdLeaves.size()<<"  estimated size = "<<estimatedSize<<std::endl<<std::endl;


    return !_kdNodes.empty();
}

void KdTree::computeDivisions(BuildOptions& options)
{
    osg::Vec3 dimensions(_bb.xMax()-_bb.xMin(),
                         _bb.yMax()-_bb.yMin(),
                         _bb.zMax()-_bb.zMin());

#ifdef VERBOSE_OUTPUT    
    osg::notify(osg::NOTICE)<<"computeDivisions("<<options._maxNumLevels<<") "<<dimensions<< " { "<<std::endl;
#endif

    _axisStack.reserve(options._maxNumLevels);
 
    for(int level=0; level<options._maxNumLevels; ++level)
    {
        int axis = 0;
        if (dimensions[0]>=dimensions[1])
        {
            if (dimensions[0]>=dimensions[2]) axis = 0;
            else axis = 2;
        }
        else if (dimensions[1]>=dimensions[2]) axis = 1;
        else axis = 2;

        _axisStack.push_back(axis);
        dimensions[axis] /= 2.0f;

#ifdef VERBOSE_OUTPUT    
        osg::notify(osg::NOTICE)<<"  "<<level<<", "<<dimensions<<", "<<axis<<std::endl;
#endif
    }

#ifdef VERBOSE_OUTPUT    
    osg::notify(osg::NOTICE)<<"}"<<std::endl;
#endif
}

int KdTree::divide(BuildOptions& options, osg::BoundingBox& bb, int nodeIndex, unsigned int level)
{
    KdNode& node = getNode(nodeIndex);

    bool needToDivide = level < _axisStack.size() &&
                        (node.first<0 && node.second>options._targetNumTrianglesPerLeaf);
                        
    if (!needToDivide)
    {
        if (node.first<0)
        {
            int istart = -node.first-1;
            int iend = istart+node.second-1;
    
            // leaf is done, now compute bound on it.
            node.bb.init();
            for(int i=istart; i<=iend; ++i)
            {
                const Triangle& tri = _triangles[_primitiveIndices[i]];
                const osg::Vec3& v1 = (*_vertices)[tri._p1];
                const osg::Vec3& v2 = (*_vertices)[tri._p2];
                const osg::Vec3& v3 = (*_vertices)[tri._p3];
                node.bb.expandBy(v1);
                node.bb.expandBy(v2);
                node.bb.expandBy(v3);
                
                float epsilon = 1e-6;
                node.bb._min.x() -= epsilon;
                node.bb._min.y() -= epsilon;
                node.bb._min.z() -= epsilon;
                node.bb._max.x() += epsilon;
                node.bb._max.y() += epsilon;
                node.bb._max.z() += epsilon;
            }

#ifdef VERBOSE_OUTPUT    
            if (!node.bb.valid())
            {
                osg::notify(osg::NOTICE)<<"After reset "<<node.first<<","<<node.second<<std::endl;
                osg::notify(osg::NOTICE)<<"  bb._min ("<<node.bb._min<<")"<<std::endl;
                osg::notify(osg::NOTICE)<<"  bb._max ("<<node.bb._max<<")"<<std::endl;
            }
            else
            {
                osg::notify(osg::NOTICE)<<"Set bb for nodeIndex = "<<nodeIndex<<std::endl;
            }
#endif
        }

        return nodeIndex;

    }

    int axis = _axisStack[level];

#ifdef VERBOSE_OUTPUT    
    osg::notify(osg::NOTICE)<<"divide("<<nodeIndex<<", "<<level<< "), axis="<<axis<<std::endl;
#endif

    if (node.first<0)
    {    
        // leaf node as first <= 0, so look at dividing it.
        
        int istart = -node.first-1;
        int iend = istart+node.second-1;

        //osg::notify(osg::NOTICE)<<"  divide leaf"<<std::endl;
        
        float original_min = bb._min[axis];
        float original_max = bb._max[axis];

        float mid = (original_min+original_max)*0.5f;

        int originalLeftChildIndex = 0;
        int originalRightChildIndex = 0;

        {
            //osg::Vec3Array* vertices = kdTree._vertices.get();
            int left = istart;
            int right = iend;
            
            while(left<right)
            {
                while(left<right && (_centers[_primitiveIndices[left]][axis]<=mid)) { ++left; }

                while(left<right && (_centers[_primitiveIndices[right]][axis]>mid)) { --right; }
                
                while(left<right && (_centers[_primitiveIndices[right]][axis]>mid)) { --right; }

                if (left<right)
                {
                    std::swap(_primitiveIndices[left], _primitiveIndices[right]);
                    ++left;
                    --right;
                }
            }
            
            if (left==right)
            {
                if (_centers[_primitiveIndices[left]][axis]<=mid) ++left;
                else --right;
            }
            
            KdNode leftLeaf(-istart-1, (right-istart)+1);
            KdNode rightLeaf(-left-1, (iend-left)+1);

#if 0
            osg::notify(osg::NOTICE)<<"In  node.first     ="<<node.first     <<" node.second     ="<<node.second<<std::endl;
            osg::notify(osg::NOTICE)<<"    leftLeaf.first ="<<leftLeaf.first <<" leftLeaf.second ="<<leftLeaf.second<<std::endl;
            osg::notify(osg::NOTICE)<<"    rightLeaf.first="<<rightLeaf.first<<" rightLeaf.second="<<rightLeaf.second<<std::endl;
            osg::notify(osg::NOTICE)<<"    left="<<left<<" right="<<right<<std::endl;

            if (node.second != (leftLeaf.second +rightLeaf.second))
            {
                osg::notify(osg::NOTICE)<<"*** Error in size, leaf.second="<<node.second
                                        <<", leftLeaf.second="<<leftLeaf.second
                                        <<", rightLeaf.second="<<rightLeaf.second<<std::endl;
            }
            else
            {
                osg::notify(osg::NOTICE)<<"Size OK, leaf.second="<<node.second
                                        <<", leftLeaf.second="<<leftLeaf.second
                                        <<", rightLeaf.second="<<rightLeaf.second<<std::endl;
            }
#endif

            if (leftLeaf.second<=0)
            {
                //osg::notify(osg::NOTICE)<<"LeftLeaf empty"<<std::endl;
                originalLeftChildIndex = 0;
                originalRightChildIndex = addNode(rightLeaf);
            }
            else if (rightLeaf.second<=0)
            {
                //osg::notify(osg::NOTICE)<<"RightLeaf empty"<<std::endl;
                originalLeftChildIndex = addNode(leftLeaf);
                originalRightChildIndex = 0;
            }
            else
            {
                originalLeftChildIndex = addNode(leftLeaf);
                originalRightChildIndex = addNode(rightLeaf);
            }
        }

        
        float restore = bb._max[axis];
        bb._max[axis] = mid;

        //osg::notify(osg::NOTICE)<<"  divide leftLeaf "<<kdTree.getNode(nodeNum).first<<std::endl;
        int leftChildIndex = originalLeftChildIndex!=0 ? divide(options, bb, originalLeftChildIndex, level+1) : 0;

        bb._max[axis] = restore;
        
        restore = bb._min[axis];
        bb._min[axis] = mid;

        //osg::notify(osg::NOTICE)<<"  divide rightLeaf "<<kdTree.getNode(nodeNum).second<<std::endl;
        int rightChildIndex = originalRightChildIndex!=0 ? divide(options, bb, originalRightChildIndex, level+1) : 0;
        
        bb._min[axis] = restore;
        
        getNode(nodeIndex).first = leftChildIndex;
        getNode(nodeIndex).second = rightChildIndex; 
        
        getNode(nodeIndex).bb.init();
        if (leftChildIndex!=0) getNode(nodeIndex).bb.expandBy(getBoundingBox(leftChildIndex));
        if (rightChildIndex!=0) getNode(nodeIndex).bb.expandBy(getBoundingBox(rightChildIndex));

        if (!getNode(nodeIndex).bb.valid())
        {
            osg::notify(osg::NOTICE)<<"leftChildIndex="<<leftChildIndex<<" && originalLeftChildIndex="<<originalLeftChildIndex<<std::endl;
            osg::notify(osg::NOTICE)<<"rightChildIndex="<<rightChildIndex<<" && originalRightChildIndex="<<originalRightChildIndex<<std::endl;

            osg::notify(osg::NOTICE)<<"Invalid BB leftChildIndex="<<leftChildIndex<<", "<<rightChildIndex<<std::endl;
            osg::notify(osg::NOTICE)<<"  bb._min ("<<getNode(nodeIndex).bb._min<<")"<<std::endl;
            osg::notify(osg::NOTICE)<<"  bb._max ("<<getNode(nodeIndex).bb._max<<")"<<std::endl;
            
            if (leftChildIndex!=0)
            {
                osg::notify(osg::NOTICE)<<"  getBoundingBox(leftChildIndex) min = "<<getBoundingBox(leftChildIndex)._min<<std::endl;
                osg::notify(osg::NOTICE)<<"                                 max = "<<getBoundingBox(leftChildIndex)._max<<std::endl;
            }
            if (rightChildIndex!=0)
            {
                osg::notify(osg::NOTICE)<<"  getBoundingBox(rightChildIndex) min = "<<getBoundingBox(rightChildIndex)._min<<std::endl;
                osg::notify(osg::NOTICE)<<"                                 max = "<<getBoundingBox(rightChildIndex)._max<<std::endl;
            }
        }

    }
    else
    {
        osg::notify(osg::NOTICE)<<"NOT expecting to get here"<<std::endl;
    }
    
    return nodeIndex;
    
}

bool KdTree::intersect(const KdNode& node, const RayData& rayData, osg::Vec3 ls, osg::Vec3 le, LineSegmentIntersections& intersections) const
{
    //osg::notify(osg::NOTICE)<<"  Intersect "<<&node<<std::endl;
    if (!intersectAndClip(ls, le, node.bb)) return false;

#if 0
    {
        osg::notify(osg::NOTICE)<<"Failed intersectAndClip("<<s<<","<<e<<")"<<std::endl;
        osg::notify(osg::NOTICE)<<"  bb._min ("<<node.bb._min<<")"<<std::endl;
        osg::notify(osg::NOTICE)<<"  bb._max ("<<node.bb._max<<")"<<std::endl;
        return false;
    }
#endif

    int numIntersectionsBefore = intersections.size();

    if (node.first<0)
    {
        // treat as a leaf

        //osg::notify(osg::NOTICE)<<"KdTree::intersect("<<&leaf<<")"<<std::endl;
        int istart = -node.first-1;
        int iend = istart + node.second;
        
        for(int i=istart; i<iend; ++i)
        {
            const Triangle& tri = _triangles[_primitiveIndices[i]];
            const osg::Vec3& v1 = (*_vertices)[tri._p1];
            const osg::Vec3& v2 = (*_vertices)[tri._p2];
            const osg::Vec3& v3 = (*_vertices)[tri._p3];
            // osg::notify(osg::NOTICE)<<"   tri("<<tri._p1<<","<<tri._p2<<","<<tri._p3<<")"<<std::endl;

            if (v1==v2 || v2==v3 || v1==v3) continue;

            osg::Vec3 v12 = v2-v1;
            osg::Vec3 n12 = v12^rayData._d;
            float ds12 = (rayData._s-v1)*n12;
            float d312 = (v3-v1)*n12;
            if (d312>=0.0f)
            {
                if (ds12<0.0f) continue;
                if (ds12>d312) continue;
            }
            else                     // d312 < 0
            {
                if (ds12>0.0f) continue;
                if (ds12<d312) continue;
            }

            osg::Vec3 v23 = v3-v2;
            osg::Vec3 n23 = v23^rayData._d;
            float ds23 = (rayData._s-v2)*n23;
            float d123 = (v1-v2)*n23;
            if (d123>=0.0f)
            {
                if (ds23<0.0f) continue;
                if (ds23>d123) continue;
            }
            else                     // d123 < 0
            {
                if (ds23>0.0f) continue;
                if (ds23<d123) continue;
            }

            osg::Vec3 v31 = v1-v3;
            osg::Vec3 n31 = v31^rayData._d;
            float ds31 = (rayData._s-v3)*n31;
            float d231 = (v2-v3)*n31;
            if (d231>=0.0f)
            {
                if (ds31<0.0f) continue;
                if (ds31>d231) continue;
            }
            else                     // d231 < 0
            {
                if (ds31>0.0f) continue;
                if (ds31<d231) continue;
            }


            float r3;
            if (ds12==0.0f) r3=0.0f;
            else if (d312!=0.0f) r3 = ds12/d312;
            else continue; // the triangle and the line must be parallel intersection.

            float r1;
            if (ds23==0.0f) r1=0.0f;
            else if (d123!=0.0f) r1 = ds23/d123;
            else continue; // the triangle and the line must be parallel intersection.

            float r2;
            if (ds31==0.0f) r2=0.0f;
            else if (d231!=0.0f) r2 = ds31/d231;
            else continue; // the triangle and the line must be parallel intersection.

            float total_r = (r1+r2+r3);
            if (total_r!=1.0f)
            {
                if (total_r==0.0f) continue; // the triangle and the line must be parallel intersection.
                float inv_total_r = 1.0f/total_r;
                r1 *= inv_total_r;
                r2 *= inv_total_r;
                r3 *= inv_total_r;
            }

            osg::Vec3 in = v1*r1+v2*r2+v3*r3;
            if (!in.valid())
            {
                osg::notify(osg::WARN)<<"Warning:: Picked up error in TriangleIntersect"<<std::endl;
                osg::notify(osg::WARN)<<"   ("<<v1<<",\t"<<v2<<",\t"<<v3<<")"<<std::endl;
                osg::notify(osg::WARN)<<"   ("<<r1<<",\t"<<r2<<",\t"<<r3<<")"<<std::endl;
                continue;
            }

            float d = (in-rayData._s)*rayData._d;

            if (d<0.0f) continue;
            if (d>rayData._length) continue;

            osg::Vec3 normal = v12^v23;
            normal.normalize();

            float r = d* rayData._inverse_length;

            LineSegmentIntersection intersection;
            intersection.ratio = r;
            intersection.primitiveIndex = _primitiveIndices[i];
            intersection.intersectionPoint = in;
            intersection.intersectionNormal = normal;

            intersection.indexList.push_back(tri._p1);
            intersection.indexList.push_back(tri._p2);
            intersection.indexList.push_back(tri._p3);

            intersection.ratioList.push_back(r1);
            intersection.ratioList.push_back(r2);
            intersection.ratioList.push_back(r3);

            intersections.insert(intersection);

            // osg::notify(osg::NOTICE)<<"  got intersection ("<<in<<") ratio="<<r<<std::endl;
        }
    }
    else
    {
        if (node.first>0) intersect(getNode(node.first), rayData, ls, le, intersections);
        if (node.second>0) intersect(getNode(node.second), rayData, ls, le, intersections);
    }

    return numIntersectionsBefore != intersections.size();
}

bool KdTree::intersectAndClip(osg::Vec3& s, osg::Vec3& e, const osg::BoundingBox& bb) const
{
    //return true;

    //if (!bb.valid()) return true;

    // compate s and e against the xMin to xMax range of bb.
    if (s.x()<=e.x())
    {

        // trivial reject of segment wholely outside.
        if (e.x()<bb.xMin()) return false;
        if (s.x()>bb.xMax()) return false;

        if (s.x()<bb.xMin())
        {
            // clip s to xMin.
            s = s+(e-s)*(bb.xMin()-s.x())/(e.x()-s.x());
        }

        if (e.x()>bb.xMax())
        {
            // clip e to xMax.
            e = s+(e-s)*(bb.xMax()-s.x())/(e.x()-s.x());
        }
    }
    else
    {
        if (s.x()<bb.xMin()) return false;
        if (e.x()>bb.xMax()) return false;

        if (e.x()<bb.xMin())
        {
            // clip s to xMin.
            e = s+(e-s)*(bb.xMin()-s.x())/(e.x()-s.x());
        }

        if (s.x()>bb.xMax())
        {
            // clip e to xMax.
            s = s+(e-s)*(bb.xMax()-s.x())/(e.x()-s.x());
        }
    }

    // compate s and e against the yMin to yMax range of bb.
    if (s.y()<=e.y())
    {

        // trivial reject of segment wholely outside.
        if (e.y()<bb.yMin()) return false;
        if (s.y()>bb.yMax()) return false;

        if (s.y()<bb.yMin())
        {
            // clip s to yMin.
            s = s+(e-s)*(bb.yMin()-s.y())/(e.y()-s.y());
        }

        if (e.y()>bb.yMax())
        {
            // clip e to yMax.
            e = s+(e-s)*(bb.yMax()-s.y())/(e.y()-s.y());
        }
    }
    else
    {
        if (s.y()<bb.yMin()) return false;
        if (e.y()>bb.yMax()) return false;

        if (e.y()<bb.yMin())
        {
            // clip s to yMin.
            e = s+(e-s)*(bb.yMin()-s.y())/(e.y()-s.y());
        }

        if (s.y()>bb.yMax())
        {
            // clip e to yMax.
            s = s+(e-s)*(bb.yMax()-s.y())/(e.y()-s.y());
        }
    }

    // compate s and e against the zMin to zMax range of bb.
    if (s.z()<=e.z())
    {

        // trivial reject of segment wholely outside.
        if (e.z()<bb.zMin()) return false;
        if (s.z()>bb.zMax()) return false;

        if (s.z()<bb.zMin())
        {
            // clip s to zMin.
            s = s+(e-s)*(bb.zMin()-s.z())/(e.z()-s.z());
        }

        if (e.z()>bb.zMax())
        {
            // clip e to zMax.
            e = s+(e-s)*(bb.zMax()-s.z())/(e.z()-s.z());
        }
    } 
    else
    {
        if (s.z()<bb.zMin()) return false;
        if (e.z()>bb.zMax()) return false;

        if (e.z()<bb.zMin())
        {
            // clip s to zMin.
            e = s+(e-s)*(bb.zMin()-s.z())/(e.z()-s.z());
        }

        if (s.z()>bb.zMax())
        {
            // clip e to zMax.
            s = s+(e-s)*(bb.zMax()-s.z())/(e.z()-s.z());
        }
    }
    
    // osg::notify(osg::NOTICE)<<"clampped segment "<<s<<" "<<e<<std::endl;
    
    // if (s==e) return false;

    return true;    
}

bool KdTree::intersect(const osg::Vec3& start, const osg::Vec3& end, LineSegmentIntersections& intersections) const
{
    RayData rayData(start,end);
    return intersect(getNode(0), rayData, start, end, intersections);
}

////////////////////////////////////////////////////////////////////////////////
//
// KdTreeBuilder
KdTreeBuilder::KdTreeBuilder():
    osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN)
{            
    _kdTreePrototype = new osg::KdTree;
}

KdTreeBuilder::KdTreeBuilder(const KdTreeBuilder& rhs):
    osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN),
    _kdTreePrototype(rhs._kdTreePrototype),
    _buildOptions(rhs._buildOptions)
{
}

void KdTreeBuilder::apply(osg::Geode& geode)
{
    for(unsigned int i=0; i<geode.getNumDrawables(); ++i)
    {            

        osg::Geometry* geom = geode.getDrawable(i)->asGeometry();
        if (geom)
        {
            osg::KdTree* previous = dynamic_cast<osg::KdTree*>(geom->getShape());
            if (previous) continue;

            osg::ref_ptr<osg::KdTree> kdTree = dynamic_cast<osg::KdTree*>(_kdTreePrototype->cloneType());

            if (kdTree->build(_buildOptions, geom))
            {
                geom->setShape(kdTree.get());
            }
        }   
    }
}