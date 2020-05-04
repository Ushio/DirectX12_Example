import hou
import json
import os
import time

# node: hou.ObjNode
def exportObjGeometry(node):
    beg_time = time.time()

    if node.renderNode() is None:
        print("    This dosen't include any renderNode.")
        return

    # print("{}".format(node.worldTransform()))
    rNode = node.renderNode() # hou.SopNode
    rGeom = rNode.geometry() # hou.Geometry

    # print("{}".format(type(rNode)))

    primitiveType = ''
    for prim in rGeom.prims():
        if prim.type() == hou.primType.Polygon:
            primitiveType = 'Polygon'
            break

    data = {
        'type' : primitiveType,
        'xform' : node.worldTransform().asTuple(),
    }

    # 'triangles'
    # traverse points
    # points = []
    # for point in rGeom.points():
    #     p = point.position()
    #     points.append(p.x())
    #     points.append(p.y())
    #     points.append(p.z())

    # Get Points Attributes
    Points = {}
    
    for point in rGeom.pointAttribs():
        # https://www.sidefx.com/docs/houdini/hom/hou/attribData.html
        if point.dataType() == hou.attribData.Float:
            values = rGeom.pointFloatAttribValues(point.name())
            Points[point.name()] = values
        print('    PointAttrib: ' + point.name())

    data['Points'] = Points

    # traverse primitives
    skippedPrimitive = 0
    PointNum = [] # for Vertices
    for prim in rGeom.prims():
        if prim.type() != hou.primType.Polygon or prim.numVertices() != 3:
            skippedPrimitive += 1 
            continue
        
        for vertex in prim.vertices():
            PointNum.append(vertex.point().number())

    Vertices = {
        'Point Num' : PointNum
    }

    # Get Vertices Attributes
    for vert in rGeom.vertexAttribs():
        # https://www.sidefx.com/docs/houdini/hom/hou/attribData.html
        if vert.dataType() == hou.attribData.Float:
            values = rGeom.vertexFloatAttribValues(vert.name())
            Vertices[vert.name()] = values
        print('    VertAttrib: ' + vert.name())

    data['Vertices'] = Vertices

    if skippedPrimitive != 0:
        print("    There are {} invalid prims by some reasons.".format(skippedPrimitive))
        return

    # Get Primitive Attributes
    Primitives = {}
    data['Primitives'] = Primitives
    for prim in rGeom.primAttribs():
        # https://www.sidefx.com/docs/houdini/hom/hou/attribData.html
        if prim.dataType() == hou.attribData.Float:
            values = rGeom.primFloatAttribValues(prim.name())
            Primitives[prim.name()] = values
        print('    PrimAttrib: ' + prim.name())

    # output Path
    filePath = os.path.join(hou.expandString("$HIP"), "out", node.name() + '.json')
    # print(json.dumps(data, indent=4))
    print("    Save {} to {}".format(node.name(), filePath))
    with open(filePath, 'w') as f:
        f.write(json.dumps(data, ensure_ascii=False))

    print("    It takes {} s".format(time.time() - beg_time))

# def exportObjGeometry(node): end

## entry point
objs = hou.node("/obj")

for node in objs.recursiveGlob("*", hou.nodeTypeFilter.ObjGeometry):
    name = node.name()
    print("ObjGeometry: {}".format(name))

    if node.isObjectDisplayed() == False:
        print("    This isn't displayed.")
        continue

    exportObjGeometry(node)

print("--done--")