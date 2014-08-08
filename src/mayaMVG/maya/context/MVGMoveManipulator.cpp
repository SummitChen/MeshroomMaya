#include "mayaMVG/maya/context/MVGMoveManipulator.h"
#include "mayaMVG/core/MVGGeometryUtil.h"
#include "mayaMVG/maya/context/MVGDrawUtil.h"
#include "mayaMVG/maya/MVGMayaUtil.h"
#include "mayaMVG/qt/MVGUserLog.h"
#include "mayaMVG/maya/context/MVGContext.h"
#include "openMVG/multiview/triangulation.hpp"

namespace mayaMVG {

MTypeId MVGMoveManipulator::_id(0x99222); // FIXME /!\ 


MVGMoveManipulator::MVGMoveManipulator() :
	_moveState(eMoveNone)
{	
	_manipUtils.intersectionData().pointIndex = -1;
}

MVGMoveManipulator::~MVGMoveManipulator()
{
}

void * MVGMoveManipulator::creator()
{
	return new MVGMoveManipulator();
}

MStatus MVGMoveManipulator::initialize()
{
	return MS::kSuccess;
}

void MVGMoveManipulator::postConstructor()
{
	registerForMouseMove();
}

void MVGMoveManipulator::draw(M3dView & view, const MDagPath & path,
                               M3dView::DisplayStyle style, M3dView::DisplayStatus dispStatus)
{
	DisplayData* data = MVGProjectWrapper::instance().getCachedDisplayData(view);
	if(!data)
		return;
	
	short mousex, mousey;
	MPoint mousePoint = updateMouse(view, data, mousex, mousey);
    
	view.beginGL();
    
    // CLEAN the input maya OpenGL State
    glDisable(GL_POLYGON_STIPPLE);
    glDisable(GL_LINE_STIPPLE);
    
    {
        // Enable gl picking
        // Will call manipulator::doPress/doRelease 
        MGLuint glPickableItem;
        glFirstHandle(glPickableItem);
        colorAndName(view, glPickableItem, true, mainColor());

        // Preview 3D 
        _manipUtils.drawPreview3D();

        // Draw	
        MVGDrawUtil::begin2DDrawing(view);
        MVGDrawUtil::drawCircle(0, 0, 1, 5); // needed - FIXME

        if(MVGMayaUtil::isActiveView(view))
        {		
            drawCursor(mousex, mousey);
            drawIntersections(view);

            Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
            //if(modifiers & Qt::NoModifier) { // Does not work
            if(!(modifiers & Qt::ControlModifier) && !(modifiers & Qt::ShiftModifier)) {
                switch(_moveState)
                {
                    case eMoveNone:
                        break;
                    case eMovePoint:
                        drawTriangulationDisplay(view, data, mousex, mousey);
                        break;
                    case eMoveEdge:
                        short x, y;
                        MPoint P1 = mousePoint + _manipUtils.intersectionData().edgeRatio * _manipUtils.intersectionData().edgeHeight2D;
                        MPoint P0 = mousePoint  - (1 - _manipUtils.intersectionData().edgeRatio) * _manipUtils.intersectionData().edgeHeight2D;
                        glBegin(GL_LINES);
                            MVGGeometryUtil::cameraToView(view, P1, x, y);
                            glVertex2f(x, y);
                            MVGGeometryUtil::cameraToView(view, P0, x, y);
                            glVertex2f(x, y);
                        glEnd();
                        break;
                }
            }
        }

        MVGDrawUtil::end2DDrawing();
    }   
    view.endGL();
}

MStatus MVGMoveManipulator::doPress(M3dView& view)
{
	// Left Button
	Qt::MouseButtons mouseButtons = QApplication::mouseButtons();
	if(!(mouseButtons & Qt::LeftButton))
		return MS::kFailure;

	DisplayData* data = MVGProjectWrapper::instance().getCachedDisplayData(view);
    if(!data)
		return MS::kFailure;
    
	short mousex, mousey;
	MPoint mousePoint = updateMouse(view, data, mousex, mousey);
	
	MVGManipulatorUtil::IntersectionData& intersectionData = _manipUtils.intersectionData();	
	_manipUtils.updateIntersectionState(view, data, mousex, mousey);
    
    Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
	switch(_manipUtils.intersectionState())
	{
		case MVGManipulatorUtil::eIntersectionNone:
			break;
		case MVGManipulatorUtil::eIntersectionPoint:
		{	
			if((modifiers & Qt::ShiftModifier) || (modifiers & Qt::ControlModifier))
			{
                // Update face informations
                MVGMesh mesh(intersectionData.meshName);
                intersectionData.facePointIndexes.clear();
                MIntArray connectedFaceIndex = mesh.getConnectedFacesToVertex(intersectionData.pointIndex);
                if(connectedFaceIndex.length() > 0)
                    intersectionData.facePointIndexes = mesh.getFaceVertices(connectedFaceIndex[0]);						
			}
			_moveState = eMovePoint;
			break;
		}
        case MVGManipulatorUtil::eIntersectionEdge:			
            if(_manipUtils.computeEdgeIntersectionData(view, data, mousePoint))
                _moveState = eMoveEdge;
            break;
	}
	
	return MPxManipulatorNode::doPress(view);
}

MStatus MVGMoveManipulator::doRelease(M3dView& view)
{	
	DisplayData* data = MVGProjectWrapper::instance().getCachedDisplayData(view);
	if(!data)
		return MS::kFailure;
	
	// Undo/Redo
	MVGEditCmd* cmd = NULL;
	if(!_manipUtils.getContext()) {
	   LOG_ERROR("invalid context object.")
	   return MS::kFailure;
	}
	
	short mousex, mousey;
	MPoint mousePoint = updateMouse(view, data, mousex, mousey);
	
	MVGManipulatorUtil::IntersectionData& intersectionData = _manipUtils.intersectionData();
    Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
	switch(_moveState) {
		case eMoveNone:
			break;
		case eMovePoint:
		{
			if((modifiers & Qt::ShiftModifier) || (modifiers & Qt::ControlModifier))
			{           
                if(_manipUtils.previewFace3D().length() == 0)
                    break;

                MDagPath meshPath;
                MVGMayaUtil::getDagPathByName(intersectionData.meshName.c_str(), meshPath);

                _manipUtils.addUpdateFaceCommand(cmd, meshPath, _manipUtils.previewFace3D(), intersectionData.facePointIndexes);
                _manipUtils.previewFace3D().clear();
                intersectionData.facePointIndexes.clear();
            }
            else {
                MPoint triangulatedPoint;
                if(!triangulate(view, intersectionData, mousePoint, triangulatedPoint))
                    break;

                MDagPath meshPath;
                MVGMayaUtil::getDagPathByName(intersectionData.meshName.c_str(), meshPath);

                MPointArray newPoints;
                newPoints.append(triangulatedPoint);
                MIntArray indexes;
                indexes.append(intersectionData.pointIndex);

                _manipUtils.addUpdateFaceCommand(cmd, meshPath, newPoints, indexes);
                intersectionData.facePointIndexes.clear();
            }
			break;
		}		
		case eMoveEdge: 
		{
            if((modifiers & Qt::ShiftModifier) || (modifiers & Qt::ControlModifier))
			{ 
                if(_manipUtils.previewFace3D().length() == 0)
                    break;

                MDagPath meshPath;
                MVGMayaUtil::getDagPathByName(intersectionData.meshName.c_str(), meshPath);

                MPointArray edgePoints;
                edgePoints.append(_manipUtils.previewFace3D()[2]);
                edgePoints.append(_manipUtils.previewFace3D()[3]);
                _manipUtils.addUpdateFaceCommand(cmd, meshPath, edgePoints, intersectionData.edgePointIndexes);
                _manipUtils.previewFace3D().clear();
                intersectionData.facePointIndexes.clear();
            }
			else {
                MPointArray triangulatedPoints;
                triangulateEdge(view, intersectionData, mousePoint, triangulatedPoints);
                MDagPath meshPath;
                MVGMayaUtil::getDagPathByName(intersectionData.meshName.c_str(), meshPath);
                _manipUtils.addUpdateFaceCommand(cmd, meshPath, triangulatedPoints, intersectionData.edgePointIndexes);
                intersectionData.facePointIndexes.clear();
            }
			break;
		}
	}

	_moveState = eMoveNone;
	return MPxManipulatorNode::doRelease(view);
}

MStatus MVGMoveManipulator::doMove(M3dView& view, bool& refresh)
{	
	refresh = true;
	DisplayData* data = MVGProjectWrapper::instance().getCachedDisplayData(view);
	if(!data)
		return MS::kFailure;

	short mousex, mousey;
	mousePosition(mousex, mousey);
	// TODO: intersect 2D point (from camera object)
	//       or intersect 2D edge (from camera object)
	//       or intersect 3D point (fetched point from mesh object)
	//if(MVGProjectWrapper::instance().getCacheMeshToPointArray().size() > 0)
	if(data->allPoints2D.size() > 0)
		_manipUtils.updateIntersectionState(view, data, mousex, mousey);
	return MPxManipulatorNode::doMove(view, refresh);
}

MStatus MVGMoveManipulator::doDrag(M3dView& view)
{		
	DisplayData* data = MVGProjectWrapper::instance().getCachedDisplayData(view);
	if(!data)
		return MS::kFailure;
	
	short mousex, mousey;
	MPoint mousePoint = updateMouse(view, data, mousex, mousey);
	
	MVGManipulatorUtil::IntersectionData& intersectionData = _manipUtils.intersectionData();
	std::vector<MVGPoint2D>& meshPoints = data->allPoints2D[intersectionData.meshName];
    
    Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
	switch(_moveState) {
		case eMoveNone:
			break;
		case eMovePoint:
            if(modifiers & Qt::ControlModifier)
            {
                if(meshPoints[intersectionData.pointIndex].movableState >= eMovableInSamePlane)
                    computeTmpFaceOnMovePoint(view, data, mousePoint);
                else
                    USER_WARNING("Can't move this point")
            }
            else if(modifiers & Qt::ShiftModifier)
            {
                if(meshPoints[intersectionData.pointIndex].movableState == eMovableRecompute)
                    computeTmpFaceOnMovePoint(view, data, mousePoint, true);
                else
                    USER_WARNING("Can't move this point")
            }
            else {
                _manipUtils.previewFace3D().clear();
                _manipUtils.intersectionState() = MVGManipulatorUtil::eIntersectionNone;
            }
			break;
		case eMoveEdge: 
		{
            if(modifiers & Qt::ControlModifier)
            {
                if(meshPoints[intersectionData.edgePointIndexes[0]].movableState >= eMovableInSamePlane
                    && meshPoints[intersectionData.edgePointIndexes[1]].movableState >= eMovableInSamePlane)
                {
                    computeTmpFaceOnMoveEdge(view, data, mousePoint);
                }	 
            }
            else if(modifiers & Qt::ShiftModifier)
            {
                if(meshPoints[intersectionData.edgePointIndexes[0]].movableState >= eMovableInSamePlane
                    && meshPoints[intersectionData.edgePointIndexes[1]].movableState >= eMovableInSamePlane)
                {
                    computeTmpFaceOnMoveEdge(view, data, mousePoint, true);
                }
            }
            else {
                _manipUtils.previewFace3D().clear();
                _manipUtils.intersectionState() = MVGManipulatorUtil::eIntersectionNone;
            }
			break;
		}
	}
	
	return MPxManipulatorNode::doDrag(view);
}

void MVGMoveManipulator::preDrawUI(const M3dView& view)
{
}

void MVGMoveManipulator::setContext(MVGContext* ctx)
{
	_manipUtils.setContext(ctx);
}

MPoint MVGMoveManipulator::updateMouse(M3dView& view, DisplayData* data, short& mousex, short& mousey)
{
	mousePosition(mousex, mousey);
	MPoint mousePointInCameraCoord;
	MVGGeometryUtil::viewToCamera(view, mousex, mousey, mousePointInCameraCoord);
	
	return mousePointInCameraCoord;
}

void MVGMoveManipulator::drawCursor(float mousex, float mousey)
{
	glColor4f(0.f, 0.f, 0.f, 0.8f);
	MVGDrawUtil::drawArrowsCursor(mousex, mousey);
	
    Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
    if(modifiers & Qt::ControlModifier)
        drawMoveInPlaneCursor(mousex, mousey);
    else if(modifiers & Qt::ShiftModifier)
        drawMoveRecomputePlaneCursor(mousex, mousey);
    else
        drawTriangulateCursor(mousex, mousey);
}



void MVGMoveManipulator::drawTriangulateCursor(float mousex, float mousey)
{
	glColor3f(0.9f, 0.5f, 0.4f);
	MVGDrawUtil::drawFullCross(mousex + 10, mousey + 10, 5, 1);
}

void MVGMoveManipulator::drawMoveInPlaneCursor(float mousex, float mousey)
{
	glColor3f(0.f, 1.f, 0.f);
	MVGDrawUtil::drawPlaneItem(mousex + 12, mousey + 10);
}

void MVGMoveManipulator::drawMoveRecomputePlaneCursor(float mousex, float mousey)
{
	glColor3f(0.f, 1.f, 1.f);
	MVGDrawUtil::drawPointCloudItem(mousex + 10, mousey + 10);
}
		
void MVGMoveManipulator::drawIntersections(M3dView& view)
{
	DisplayData* data = MVGProjectWrapper::instance().getCachedDisplayData(view);
	if(!data || data->allPoints2D.empty())
		return;

	MVGManipulatorUtil::IntersectionData& intersectionData = _manipUtils.intersectionData();
	std::vector<MVGPoint2D>& meshPoints = data->allPoints2D[intersectionData.meshName];
	
    Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
	switch(_manipUtils.intersectionState())
	{
		case MVGManipulatorUtil::eIntersectionPoint:
		{
			EPointState movableState = meshPoints[intersectionData.pointIndex].movableState;

            if(modifiers & Qt::ControlModifier)
            {
                if(movableState >= eMovableInSamePlane)
                    glColor3f(0.f, 1.f, 0.f);
                else
                    glColor3f(1.f, 0.f, 0.f);
            }
            else if(modifiers & Qt::ShiftModifier)
            {
                if(movableState == eMovableRecompute)
                    glColor4f(0.f, 1.f, 1.f, 0.8f);
                else
                    glColor3f(1.f, 0.f, 0.f);
            }
            else
            {
                glColor4f(0.3f, 0.3f, 0.6f, 0.8f);
            }

			MPoint point = MVGGeometryUtil::worldToView(view, meshPoints[intersectionData.pointIndex].point3D);
			MVGDrawUtil::drawCircle(point.x, point.y, POINT_RADIUS, 30);
			break;
		}
		case MVGManipulatorUtil::eIntersectionEdge:	
			std::vector<EPointState> movableStates;
			movableStates.push_back(meshPoints[intersectionData.edgePointIndexes[0]].movableState);
			movableStates.push_back(meshPoints[intersectionData.edgePointIndexes[1]].movableState);

            if(modifiers & Qt::ControlModifier)
            {
                if((movableStates[0] >= eMovableInSamePlane)
                    && (movableStates[1] >= eMovableInSamePlane))
                {
                    glColor3f(0.f, 1.f, 0.f);
                }
                else
                {
                    glColor3f(1.f, 0.f, 0.f);
                }
            }
            else if(modifiers & Qt::ShiftModifier)
            {
                if((movableStates[0] >= eMovableInSamePlane)
                    && (movableStates[1] >= eMovableInSamePlane))
                {
                    glColor4f(0.f, 1.f, 1.f, 0.8f);
                }
                else
                {
                    glColor3f(1.f, 0.f, 0.f);
                }
            }
            else
            {
                glColor4f(0.3f, 0.3f, 0.6f, 0.8f);
            }

			glLineWidth(1.5f);
			glBegin(GL_LINES);
				MPoint point = MVGGeometryUtil::worldToView(view,  meshPoints[intersectionData.edgePointIndexes[0]].point3D);
				glVertex2f(point.x, point.y);
				point = MVGGeometryUtil::worldToView(view, meshPoints[intersectionData.edgePointIndexes[1]].point3D);
				glVertex2f(point.x, point.y);
			glEnd();	
			break;
	}
}

void MVGMoveManipulator::drawTriangulationDisplay(M3dView& view, DisplayData* data, float mousex, float mousey)
{
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(1.f, 0x5555);  
    glColor3f(0.9f, 0.5f, 0.4f);
    glBegin(GL_LINES);
        MPoint& point3D = data->allPoints2D[_manipUtils.intersectionData().meshName].at(_manipUtils.intersectionData().pointIndex).point3D;
        MPoint viewPoint = MVGGeometryUtil::worldToView(view, point3D);
        glVertex2f(viewPoint.x, viewPoint.y);
        // Mouse
        glVertex2f(mousex, mousey);
    glEnd();
    glDisable(GL_LINE_STIPPLE);
}

void MVGMoveManipulator::computeTmpFaceOnMovePoint(M3dView& view, DisplayData* data, MPoint& mousePoint, bool recompute)
{
	MVGManipulatorUtil::IntersectionData& intersectionData = _manipUtils.intersectionData();
	std::vector<MVGPoint2D>& mvgPoints = data->allPoints2D[intersectionData.meshName];

	MIntArray verticesId = intersectionData.facePointIndexes;
	MPointArray& previewFace3D = _manipUtils.previewFace3D();

	if(recompute)
	{
		MPointArray previewPoints2D;
		for(int i = 0; i < verticesId.length(); ++i)
		{
			if(intersectionData.pointIndex == verticesId[i])
				previewPoints2D.append(mousePoint);
			else
				previewPoints2D.append(mvgPoints[verticesId[i]].projectedPoint3D);
		}
		
		// Compute face		
		previewFace3D.clear();
		MVGGeometryUtil::projectFace2D(view, previewFace3D, data->camera, previewPoints2D);
	}
	else {
		// Fill previewFace3D
		MPointArray facePoints3D;
		for(int i = 0; i < verticesId.length(); ++i)
		{
			facePoints3D.append(mvgPoints[verticesId[i]].point3D);
		}
		MPoint movedPoint;
		PlaneKernel::Model model;

		MVGGeometryUtil::computePlane(facePoints3D, model);
		MVGGeometryUtil::projectPointOnPlane(mousePoint, view, model, data->camera, movedPoint);

		previewFace3D.clear();
		previewFace3D.setLength(4);
		for(int i = 0; i < verticesId.length(); ++i)
		{
			if(intersectionData.pointIndex == verticesId[i])
				previewFace3D[i] = movedPoint;
			else
				previewFace3D[i] = facePoints3D[i];
		}		
	}
}

void MVGMoveManipulator::computeTmpFaceOnMoveEdge(M3dView& view, DisplayData* data, MPoint& mousePoint, bool recompute)
{
	MVGManipulatorUtil::IntersectionData& intersectionData = _manipUtils.intersectionData();
	std::vector<MVGPoint2D>& mvgPoints = data->allPoints2D[intersectionData.meshName];
	MPointArray& previewFace3D = _manipUtils.previewFace3D();
	MIntArray verticesId = intersectionData.facePointIndexes;
	MIntArray edgeVerticesId = intersectionData.edgePointIndexes;
	MIntArray fixedVerticesId;
	for(int i = 0; i < verticesId.length(); ++i)
	{
		int found = false;
		for(int j = 0; j < edgeVerticesId.length(); ++j)
		{
			if(verticesId[i] == edgeVerticesId[j])
				found = true;
		}

		if(!found)
			fixedVerticesId.append(verticesId[i]);
	}

	// Switch order if necessary
	if(fixedVerticesId[0] == verticesId[0]
		&& fixedVerticesId[1] == verticesId[verticesId.length() - 1])
	{
		MIntArray tmp = fixedVerticesId;
		fixedVerticesId[0] = tmp[1];
		fixedVerticesId[1] = tmp[0];
	}
		
	if(recompute)
	{
		MPointArray previewPoints2D;

		// First : fixed points
		previewPoints2D.append(mvgPoints[fixedVerticesId[0]].projectedPoint3D);
		previewPoints2D.append(mvgPoints[fixedVerticesId[1]].projectedPoint3D);

		// Then : mousePoints computed with egdeHeight and ratio							
		MPoint P3 = mousePoint + intersectionData.edgeRatio * intersectionData.edgeHeight2D;
		previewPoints2D.append(P3);
		MPoint P4 = mousePoint  - (1 - intersectionData.edgeRatio) * intersectionData.edgeHeight2D;
		previewPoints2D.append(P4);
		
		// Only set the new points to keep a connected face
		previewFace3D.clear();
		MVGGeometryUtil::projectFace2D(view, previewFace3D, data->camera, previewPoints2D, true, -intersectionData.edgeHeight3D);

		// Check points order	
		MVector AD =  previewFace3D[3] - previewFace3D[0];
		MVector BC =  previewFace3D[2] - previewFace3D[1];

		if(MVGGeometryUtil::edgesIntersection(previewFace3D[0], previewFace3D[1], AD, BC))
		{
			MPointArray tmp = previewFace3D;
			previewFace3D[3] = tmp[2];
			previewFace3D[2] = tmp[3];
		}
	}
	else {
		
		// Fill previewFace3D
		MPointArray facePoints3D;
		for(int i = 0; i < verticesId.length(); ++i)
		{
			facePoints3D.append(mvgPoints[verticesId[i]].point3D);
		}

		// Compute plane with old face points
		MPoint movedPoint;
		PlaneKernel::Model model;
		MVGGeometryUtil::computePlane(facePoints3D, model);

		previewFace3D.clear();
		previewFace3D.setLength(4);		
		previewFace3D[0] = mvgPoints[fixedVerticesId[0]].point3D;
		previewFace3D[1] = mvgPoints[fixedVerticesId[1]].point3D;

		// Project new points on plane			
		MPoint P3 = mousePoint + _manipUtils.intersectionData().edgeRatio * _manipUtils.intersectionData().edgeHeight2D;
		MVGGeometryUtil::projectPointOnPlane(P3, view, model, data->camera, movedPoint);
		previewFace3D[3] = movedPoint;

		// Keep 3D length
		MPoint lastPoint3D = movedPoint - _manipUtils.intersectionData().edgeHeight3D;
		MPoint lastPoint2D;
		MVGGeometryUtil::worldToCamera(view, lastPoint3D, lastPoint2D);
		MVGGeometryUtil::projectPointOnPlane(lastPoint2D, view, model, data->camera, movedPoint);
		previewFace3D[2] = movedPoint;

	}
}

bool MVGMoveManipulator::triangulate(M3dView& view, MVGManipulatorUtil::IntersectionData& intersectionData, MPoint& mousePointInCameraCoord, MPoint& resultPoint3D)
{
	// Points in camera coordinates
	MPointArray points2D;
	points2D.append(mousePointInCameraCoord);

	std::map<std::string, DisplayData>& displayDataCache = MVGProjectWrapper::instance().getDisplayDataCache();
    if(displayDataCache.size() == 1)
    {
        USER_WARNING("Can't triangulate with the same camera")
        return false;
    }

    MDagPath cameraPath;
	view.getCamera(cameraPath);
	DisplayData* otherData;
	for(std::map<std::string, DisplayData>::iterator it = displayDataCache.begin(); it != displayDataCache.end(); ++it)
	{
		if(it->first != cameraPath.fullPathName().asChar())
			otherData = (&it->second);
	}
	if(!otherData)
		return false;
	std::vector<MVGPoint2D>& mvgPoints = otherData->allPoints2D[intersectionData.meshName];
	points2D.append(mvgPoints[intersectionData.pointIndex].projectedPoint3D);

	// MVGCameras
	 std::vector<MVGCamera> cameras;
	 cameras.push_back(MVGCamera(cameraPath));
	 cameras.push_back(otherData->camera);

	MVGGeometryUtil::triangulatePoint(points2D, cameras, resultPoint3D);
	return true;
}


bool MVGMoveManipulator::triangulateEdge(M3dView& view, MVGManipulatorUtil::IntersectionData& intersectionData, MPoint& mousePointInCameraCoord, MPointArray& resultPoint3D)
{
    
    // Edge points (moved)
    MPointArray array0, array1;
    MPoint P1 = mousePointInCameraCoord + intersectionData.edgeRatio * intersectionData.edgeHeight2D;
    array1.append(P1);
    MPoint P0 = mousePointInCameraCoord  - (1 - intersectionData.edgeRatio) * intersectionData.edgeHeight2D;
    array0.append(P0);
    
	std::map<std::string, DisplayData>& displayDataCache = MVGProjectWrapper::instance().getDisplayDataCache();
    if(displayDataCache.size() == 1)
    {
        USER_WARNING("Can't triangulate with the same camera")
        return false;
    }

    MDagPath cameraPath;
	view.getCamera(cameraPath);
	DisplayData* otherData;
	for(std::map<std::string, DisplayData>::iterator it = displayDataCache.begin(); it != displayDataCache.end(); ++it)
	{
		if(it->first != cameraPath.fullPathName().asChar())
			otherData = (&it->second);
	}
	if(!otherData)
		return false;
    
    // Edge points
	std::vector<MVGPoint2D>& mvgPoints = otherData->allPoints2D[intersectionData.meshName];
    array0.append(mvgPoints[intersectionData.edgePointIndexes[0]].projectedPoint3D);
    array1.append(mvgPoints[intersectionData.edgePointIndexes[1]].projectedPoint3D);
    
	// MVGCameras
    std::vector<MVGCamera> cameras;
    cameras.push_back(MVGCamera(cameraPath));
    cameras.push_back(otherData->camera);
        
    MPoint result;
	MVGGeometryUtil::triangulatePoint(array0, cameras, result);
    resultPoint3D.append(result);
	MVGGeometryUtil::triangulatePoint(array1, cameras, result);
    resultPoint3D.append(result);

	return true;
}

void MVGMoveManipulator::drawUI(MHWRender::MUIDrawManager& drawManager, const MHWRender::MFrameContext&) const
{
	drawManager.beginDrawable();
	drawManager.setColor(MColor(1.0, 0.0, 0.0, 0.6));
	// TODO
	drawManager.endDrawable();
}

} // namespace
