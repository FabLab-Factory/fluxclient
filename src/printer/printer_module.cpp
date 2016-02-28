#include <iostream>
#include <cmath>
#include <map>
#include <algorithm>
#include <limits>
#include <pcl/filters/voxel_grid.h>
////////////////////   fake code ////////////////////////////////////
#include <pcl/io/pcd_io.h>
//////////////////////////////////////////////////////
#include "printer_module.h"
#define likely(x)  __builtin_expect((x),1)

float d_v3(Eigen::Vector3f &a, Eigen::Vector3f &b){
  return sqrt(pow(a[0] - b[0], 2) + pow(a[1] - b[1], 2) + pow(a[2] - b[2], 2));
}

struct cone{
  // float pos[3];
  Eigen::Vector3f pos;
  float theta;
  cone(){
    pos = Eigen::Vector3f(0, 0, 0);
    theta = 0;
  }
  cone(float x, float  y, float z, float t){
    pos = Eigen::Vector3f(x, y, z);
    theta = t;
  }
  cone(cone &cone2){
    pos[0] = cone2.pos[0];
    pos[1] = cone2.pos[1];
    pos[2] = cone2.pos[2];
    theta = cone2.theta;
  }
};

struct tri_data{
  bool ok;
  Eigen::Matrix3f tri_after;
  Eigen::Affine3f tri_trans;
  // Eigen::Vector3f L_13;
  // Eigen::Vector3f L_111;
  // Eigen::Vector3f L_331;
  // Eigen::Vector3f L_23;
  // Eigen::Vector3f L_223;
  // Eigen::Vector3f L_332;
  // Eigen::Vector3f L_12;
  // Eigen::Vector3f L_112;
  // Eigen::Vector3f L_221;
  std::map<int, Eigen::Vector3f> L;
  std::map<int, bool> in_tri;
};

struct tree_node{
  int left, right, index, height;
  tree_node(int l, int r, int i, int h){
    left = l;
    right = r;
    index = i;
    height = h;
  }
};

inline std::ostream& operator<< (std::ostream& stream, const tree_node& n){
  stream << n.left << " " << n.right << " " << n.index << " " << n.height << " " << std::endl;
  return stream;
}

class SupportTree{
public:
  SupportTree(pcl::PointCloud<pcl::PointXYZ>::Ptr P);
  ~SupportTree();
  void out_as_js();

  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud;
  std::vector<tree_node> tree;
};

SupportTree::SupportTree(pcl::PointCloud<pcl::PointXYZ>::Ptr P){
  cloud = P;
}

SupportTree::~SupportTree(){
}

void SupportTree::out_as_js(){
  std::cout<< "tree = [";
  for (size_t i = 0; i < tree.size(); i += 1){
    if(tree[i].left != -1){
      std::cout<< "[";

      std::cout << "[" << (*cloud)[tree[i].index].x << "," << (*cloud)[tree[i].index].y << "," << (*cloud)[tree[i].index].z << "]";
      std::cout<< ",";

      if(tree[i].left >= 0){
        std::cout << "[" << (*cloud)[tree[i].left].x << "," << (*cloud)[tree[i].left].y << "," << (*cloud)[tree[i].left].z << "]";
        std::cout<< ",";
      }
      else{
      }
      std::cout << "[" << (*cloud)[tree[i].right].x << "," << (*cloud)[tree[i].right].y << "," << (*cloud)[tree[i].right].z << "]";
      std::cout<< ",";

      std::cout<< tree[i].height;

      std::cout<< "],";
      // std::cout<< std::endl;
    }
  }
  std::cout<< "]";
  return;
}

inline std::ostream& operator<< (std::ostream& stream, const SupportTree& t){
  for (size_t i = 0; i < t.tree.size(); i += 1){
    stream << t.tree[i].left << ", " << t.tree[i].right << ", " << t.tree[i].index << ", " << t.tree[i].height << ", " << std::endl;
  }
  return stream;
}

MeshPtr createMeshPtr(){
  MeshPtr mesh(new pcl::PolygonMesh);
  return mesh;
}

int set_point(MeshPtr triangles, std::vector< std::vector<float> > points){
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
  for (uint32_t i = 0; i < points.size(); i += 1){
    pcl::PointXYZ p;
    p.x = points[i][0];
    p.y = points[i][1];
    p.z = points[i][2];
    cloud -> push_back(p);
  }

  toPCLPointCloud2(*cloud, triangles->cloud);
  return 0;
}

int push_backFace(MeshPtr triangles, int v0, int v1, int v2){
  pcl::Vertices v;
  v.vertices.resize(3);
  v.vertices[0] = v0;
  v.vertices[1] = v1;
  v.vertices[2] = v2;
  triangles->polygons.push_back(v);
  return 0;
}

int add_on(MeshPtr base, MeshPtr add_on_mesh){
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
  fromPCLPointCloud2(base->cloud, *cloud);
  int size_to_add_on = cloud->size();

  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud2 (new pcl::PointCloud<pcl::PointXYZ>);
  fromPCLPointCloud2(add_on_mesh->cloud, *cloud2);

    // add cloud together
  *cloud += *cloud2;

  pcl::Vertices v;
  v.vertices.resize(3);
    // add faces, but shift the index for add on mesh
  for (uint32_t i = 0; i < add_on_mesh->polygons.size(); i += 1){
    v.vertices[0] = add_on_mesh->polygons[i].vertices[0] + size_to_add_on;
    v.vertices[1] = add_on_mesh->polygons[i].vertices[1] + size_to_add_on;
    v.vertices[2] = add_on_mesh->polygons[i].vertices[2] + size_to_add_on;
    base->polygons.push_back(v);
  }

  toPCLPointCloud2(*cloud, base->cloud);
  return 0;
}

int bounding_box(MeshPtr triangles, std::vector<float> &b_box){
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
  fromPCLPointCloud2(triangles->cloud, *cloud);
  float minx = std::numeric_limits<double>::infinity(), miny = std::numeric_limits<double>::infinity(), minz = std::numeric_limits<double>::infinity();
  float maxx = -1 * std::numeric_limits<double>::infinity(), maxy = -1 * std::numeric_limits<double>::infinity(), maxz = -1 * std::numeric_limits<double>::infinity();

  for (uint32_t i = 0; i < cloud->size(); i += 1){
    if((*cloud)[i].x > maxx){
      maxx = (*cloud)[i].x;
    }
    if((*cloud)[i].y > maxy){
      maxy = (*cloud)[i].y;
    }
    if((*cloud)[i].z > maxz){
      maxz = (*cloud)[i].z;
    }

    if((*cloud)[i].x < minx){
      minx = (*cloud)[i].x;
    }
    if((*cloud)[i].y < miny){
      miny = (*cloud)[i].y;
    }
    if((*cloud)[i].z < minz){
      minz = (*cloud)[i].z;
    }
  }

  b_box.resize(6);
  b_box[0] = minx;
  b_box[1] = miny;
  b_box[2] = minz;
  b_box[3] = maxx;
  b_box[4] = maxy;
  b_box[5] = maxz;

  return 0;
}

int bounding_box(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud, std::vector<float> &b_box){
  float minx = std::numeric_limits<double>::infinity(), miny = std::numeric_limits<double>::infinity(), minz = std::numeric_limits<double>::infinity();
  float maxx = -1 * std::numeric_limits<double>::infinity(), maxy = -1 * std::numeric_limits<double>::infinity(), maxz = -1 * std::numeric_limits<double>::infinity();

  for (uint32_t i = 0; i < cloud->size(); i += 1){
    if((*cloud)[i].x > maxx){
      maxx = (*cloud)[i].x;
    }
    if((*cloud)[i].y > maxy){
      maxy = (*cloud)[i].y;
    }
    if((*cloud)[i].z > maxz){
      maxz = (*cloud)[i].z;
    }

    if((*cloud)[i].x < minx){
      minx = (*cloud)[i].x;
    }
    if((*cloud)[i].y < miny){
      miny = (*cloud)[i].y;
    }
    if((*cloud)[i].z < minz){
      minz = (*cloud)[i].z;
    }
  }
  b_box.resize(6);
  b_box[0] = minx;
  b_box[1] = miny;
  b_box[2] = minz;
  b_box[3] = maxx;
  b_box[4] = maxy;
  b_box[5] = maxz;

  return 0;
}

int apply_transform(MeshPtr triangles, float x, float y, float z, float rx, float ry, float rz, float sc_x, float sc_y, float sc_z){
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
  fromPCLPointCloud2(triangles->cloud, *cloud);

  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  Eigen::Matrix4f tmpM = Eigen::Matrix4f::Identity();
  float theta; // The angle of rotation in radians

  std::vector<float> b_box;
  b_box.resize(3);
  std::vector<float> center;
  center.resize(3);

  // scale
  for (uint32_t i = 0; i < cloud->size(); i += 1){
    (*cloud)[i].x *= sc_x;
    (*cloud)[i].y *= sc_y;
    (*cloud)[i].z *= sc_z;
  }

  // move to origin
  bounding_box(cloud, b_box);
  for (int i = 0; i < 3; i += 1){
    center[i] = (b_box[i] + b_box[i + 3]) / 2;
  }
  transform = Eigen::Matrix4f::Identity();
  transform(0, 3) = - center[0];
  transform(1, 3) = - center[1];
  transform(2, 3) = - center[2];
  pcl::transformPointCloud(*cloud, *cloud, transform);

  // rotate
  transform = Eigen::Matrix4f::Identity();
  tmpM = Eigen::Matrix4f::Identity();
  theta = rx; // The angle of rotation in radians
  tmpM(1, 1) = cos (theta); //x
  tmpM(1, 2) = -sin(theta);
  tmpM(2, 1) = sin (theta);
  tmpM(2, 2) = cos (theta);
  transform *= tmpM;

  tmpM = Eigen::Matrix4f::Identity();
  theta = ry;
  tmpM(0, 0) = cos (theta); //y
  tmpM(2, 0) = -sin(theta);
  tmpM(0, 2) = sin (theta);
  tmpM(2, 2) = cos (theta);
  transform *= tmpM;

  tmpM = Eigen::Matrix4f::Identity();
  theta = rz;
  tmpM(0, 0) = cos (theta); //z
  tmpM(0, 1) = -sin(theta);
  tmpM(1, 0) = sin (theta);
  tmpM(1, 1) = cos (theta);
  transform *= tmpM;
  pcl::transformPointCloud(*cloud, *cloud, transform);

  // move to proper position
  transform = Eigen::Matrix4f::Identity();
  transform(0, 3) = x;
  transform(1, 3) = y;
  transform(2, 3) = z;
  pcl::transformPointCloud(*cloud, *cloud, transform);

  toPCLPointCloud2(*cloud, triangles->cloud);
  return 0;
}

int find_intersect(pcl::PointXYZ &a, pcl::PointXYZ &b, float floor_v, pcl::PointXYZ &p){
  // find the intersect between line a, b and plane z=floor_v
  std::vector<float> v(3);  // vetor a->b
  v[0] = b.x - a.x;
  v[1] = b.y - a.y;
  v[2] = b.z - a.z;

  float t = (floor_v - a.z) / v[2];
  p.x = a.x + t * v[0];
  p.y = a.y + t * v[1];
  p.z = a.z + t * v[2];
  return 0;
}


int cut(MeshPtr input_mesh, MeshPtr out_mesh, float floor_v){
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
  fromPCLPointCloud2(input_mesh->cloud, *cloud);

  pcl::Vertices v;
  v.vertices.resize(3);
  pcl::Vertices on, under;
  // consider serveral case

  for (uint32_t i = 0; i < input_mesh->polygons.size(); i += 1){
    // on.vertices.clear();
    // under.vertices.clear();
    pcl::Vertices on, under;
    for (uint32_t j = 0; j < 3; j += 1){

      if ((*cloud)[input_mesh->polygons[i].vertices[j]].z <= floor_v){
        under.vertices.push_back(input_mesh->polygons[i].vertices[j]);
      }
      else{
        on.vertices.push_back(input_mesh->polygons[i].vertices[j]);
      }
    }

    if(on.vertices.size() == 3){
      out_mesh->polygons.push_back(on);
    }
    else if(under.vertices.size() == 3){
      // do nothing
    }
    else if(under.vertices.size() == 2){
      pcl::PointXYZ upper_p;
      upper_p = (*cloud)[on.vertices[0]];
      for (int j = 0; j < 2; j += 1){
        pcl::PointXYZ lower_p, new_p;
        lower_p = (*cloud)[under.vertices[j]];
        find_intersect(upper_p, lower_p, floor_v, new_p);
        cloud -> push_back(new_p);
        on.vertices.push_back(cloud -> size() - 1);
      }
      out_mesh->polygons.push_back(on);
    }
    else if(under.vertices.size() == 1){
      pcl::PointXYZ mid;
      mid.x = ((*cloud)[on.vertices[0]].x + (*cloud)[on.vertices[1]].x) / 2;
      mid.y = ((*cloud)[on.vertices[0]].y + (*cloud)[on.vertices[1]].y) / 2;
      mid.z = ((*cloud)[on.vertices[0]].z + (*cloud)[on.vertices[1]].z) / 2;
      cloud -> push_back(mid);
      int mid_index = cloud -> size() - 1;

      pcl::PointXYZ intersect0, intersect1;
      find_intersect((*cloud)[on.vertices[0]], (*cloud)[under.vertices[0]], floor_v, intersect0);
      cloud -> push_back(intersect0);
      int intersect0_index = cloud -> size() - 1;
      find_intersect((*cloud)[on.vertices[1]], (*cloud)[under.vertices[0]], floor_v, intersect1);
      cloud -> push_back(intersect1);
      int intersect1_index = cloud -> size() - 1;

      pcl::Vertices v1, v2, v3;
      v1.vertices.push_back(on.vertices[0]);
      v1.vertices.push_back(intersect0_index);
      v1.vertices.push_back(mid_index);
      out_mesh->polygons.push_back(v1);

      v2.vertices.push_back(mid_index);
      v2.vertices.push_back(intersect0_index);
      v2.vertices.push_back(intersect1_index);
      out_mesh->polygons.push_back(v2);

      v3.vertices.push_back(mid_index);
      v3.vertices.push_back(intersect1_index);
      v3.vertices.push_back(on.vertices[1]);
      out_mesh->polygons.push_back(v3);
    }

  }

  toPCLPointCloud2(*cloud, out_mesh->cloud);
  return 0;
}

int STL_to_List(MeshPtr triangles, std::vector<std::vector< std::vector<float> > > &data){
  // point's data
  // data =[
  //           t1[p1[x, y, z], p2[x, y, z], p3[x, y, z]],
  //           t2[p1[x, y, z], p2[x, y, z], p3[x, y, z]],
  //           t3[p1[x, y, z], p2[x, y, z], p3[x, y, z]],
  //             ...
  //       ]
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
  fromPCLPointCloud2(triangles->cloud, *cloud);

  int v0, v1, v2;

  std::vector<float> tmpvv(3, 0.0);
  std::vector< std::vector<float> > tmpv(3, tmpvv);
  data.resize(triangles->polygons.size());

  for (size_t i = 0; i < triangles->polygons.size(); i += 1){
    data[i] = tmpv;

    v0 = triangles->polygons[i].vertices[0];
    v1 = triangles->polygons[i].vertices[1];
    v2 = triangles->polygons[i].vertices[2];

    data[i][0][0] = (*cloud)[v0].x;
    data[i][0][1] = (*cloud)[v0].y;
    data[i][0][2] = (*cloud)[v0].z;

    data[i][1][0] = (*cloud)[v1].x;
    data[i][1][1] = (*cloud)[v1].y;
    data[i][1][2] = (*cloud)[v1].z;

    data[i][2][0] = (*cloud)[v2].x;
    data[i][2][1] = (*cloud)[v2].y;
    data[i][2][2] = (*cloud)[v2].z;
      // std::cout << "  polygons[" << i << "]: " <<std::endl;
  }
  return 0;
}

int STL_to_Faces(MeshPtr triangles, std::vector< std::vector<int> > &data){
  // index of faces
  // data = [ f1[p1_index, p2_index, p3_index],
  //          f2[p1_index, p2_index, p3_index], ...
  //        ]
  data.resize(triangles->polygons.size());
  for (size_t i = 0; i < triangles->polygons.size(); i += 1){
    data[i].resize(3);
    data[i][0] = triangles->polygons[i].vertices[0];
    data[i][1] = triangles->polygons[i].vertices[1];
    data[i][2] = triangles->polygons[i].vertices[2];
  }
  return 0;
}

bool sort_by_z(const pcl::PointXYZ a,const pcl::PointXYZ b)
{
   return a.z < b.z;
}
bool sort_by_second(const std::pair<int, float> a, const std::pair<int, float> b){
  if(likely(a.second != b.second)){
    return a.second < b.second;
  }
  else{
    return a.first < b.first;
  }
}

Eigen::Vector3f sol_2(float x1, float y1, float x2, float y2){
  // return Eigen::Vector3f(y1 - y2, x2 - x1, x1 * y2 - x2 * y1);
  return Eigen::Vector3f(y1 - y2, x2 - x1, x1 * y2 - x2 * y1);
}

float sub_in(float y, float z, Eigen::Vector3f f){
  return f(0) * y + f(1) * z + f(2);
}

int preprocess(MeshPtr input_mesh, std::vector<tri_data> &preprocess_tri){
  // ref:http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.479.8237&rep=rep1&type=pdf
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
  fromPCLPointCloud2(input_mesh->cloud, *cloud);
  for (size_t i = 0; i < input_mesh -> polygons.size(); i += 1){
  // for (size_t i = 0; i < 1; i += 1){
    // TODO:(check if ok for tri)
    tri_data a;
    Eigen::Matrix3f tri_before;
    Eigen::Matrix3f tri_after;
    tri_after.setZero();
    for (size_t j = 0; j < 3; j += 1){
      tri_before(0, j) = (*cloud)[(input_mesh->polygons[i]).vertices[j]].x;
      tri_before(1, j) = (*cloud)[(input_mesh->polygons[i]).vertices[j]].y;
      tri_before(2, j) = (*cloud)[(input_mesh->polygons[i]).vertices[j]].z;
    }

    float d12 = sqrt(pow(tri_before(0, 0) - tri_before(0, 1), 2) + pow(tri_before(1, 0) - tri_before(1, 1), 2) + pow(tri_before(2, 0) - tri_before(2, 1), 2));
    float d13 = sqrt(pow(tri_before(0, 0) - tri_before(0, 2), 2) + pow(tri_before(1, 0) - tri_before(1, 2), 2) + pow(tri_before(2, 0) - tri_before(2, 2), 2));
    float d23 = sqrt(pow(tri_before(0, 1) - tri_before(0, 2), 2) + pow(tri_before(1, 1) - tri_before(1, 2), 2) + pow(tri_before(2, 1) - tri_before(2, 2), 2));
    tri_after(2, 1) = d12;
    tri_after(2, 2) = (pow(d13, 2) - pow(d23, 2) + pow(d12, 2)) / 2 / d12;
    tri_after(1, 2) = sqrt(pow(d13, 2) - pow(tri_after(2, 2), 2));
    a.tri_after = tri_after;

    for (size_t i = 0; i < 3; i += 1){
      for (size_t j = 0; j < 3; j += 1){
        tri_after(i, j) -= tri_before(i, 0);
      }
    }
    Eigen::Matrix4f tri_trans;
    tri_trans.setIdentity();
    tri_trans.block<3,3>(0,0) = tri_after * tri_before.inverse();
    for (size_t j = 0; j < 3; j += 1){
      tri_trans(j, 3) = tri_before(j, 0);
    }
    a.tri_trans = tri_trans;
    tri_after = a.tri_after;
    // std::cerr<< "a " << a.tri_trans * tri_before << std::endl;
    // std::cerr<< "tri_after " << tri_after << std::endl;

    float tmp;

    a.L[13] = sol_2(tri_after(1, 0), tri_after(2, 0), tri_after(1, 2), tri_after(2, 2));
    // std::cerr<< "a[13] " << a.L[13] << std::endl;
    a.in_tri[13] = sub_in(tri_after(1, 1), tri_after(2, 1), a.L[13]) >= 0;
    // std::cerr<< "t " << sub_in(tri_after(1, 1), tri_after(2, 1), a.L[13]) << std::endl;
    // std::cerr<< "in 13 " << a.in_tri[13] << std::endl;

    tmp = -sub_in(tri_after(1, 0), tri_after(2, 0) , Eigen::Vector3f(a.L[13](1), -a.L[13](0), 0));
    a.L[111] = Eigen::Vector3f(a.L[13](1), -a.L[13](0), tmp);
    a.in_tri[111] = sub_in(tri_after(1, 2), tri_after(2, 2), a.L[111]) >= 0;

    tmp = -sub_in(tri_after(1, 2), tri_after(2, 2) , Eigen::Vector3f(a.L[13](1), -a.L[13](0), 0));
    a.L[331] = Eigen::Vector3f(a.L[13](1), -a.L[13](0), tmp);
    a.in_tri[331] = sub_in(tri_after(1, 0), tri_after(2, 0), a.L[331]) >= 0;


    a.L[23] = sol_2(tri_after(1, 1), tri_after(2, 1), tri_after(1, 2), tri_after(2, 2));
    // std::cerr<< "a[23] " << a.L[23] << std::endl;
    a.in_tri[23] = sub_in(tri_after(1, 0), tri_after(2, 0), a.L[23]) >= 0;

    tmp = -sub_in(tri_after(1, 1), tri_after(2, 1) , Eigen::Vector3f(a.L[23](1), -a.L[23](0), 0));
    a.L[223] = Eigen::Vector3f(a.L[23](1), -a.L[23](0), tmp);
    a.in_tri[223] = sub_in(tri_after(1, 2), tri_after(2, 2), a.L[223]) >= 0;

    tmp = -sub_in(tri_after(1, 2), tri_after(2, 2) , Eigen::Vector3f(a.L[23](1), -a.L[23](0), 0));
    a.L[332] = Eigen::Vector3f(a.L[23](1), -a.L[23](0), tmp);
    a.in_tri[332] = sub_in(tri_after(1, 1), tri_after(2, 1), a.L[332]) >= 0;


    a.L[12] = sol_2(tri_after(1, 0), tri_after(2, 0), tri_after(1, 1), tri_after(2, 1));
    // std::cerr<< "a[12] " << a.L[12] << std::endl;
    a.in_tri[12] = sub_in(tri_after(1, 2), tri_after(2, 2), a.L[12]) >= 0;

    tmp = -sub_in(tri_after(1, 0), tri_after(2, 0) , Eigen::Vector3f(a.L[12](1), -a.L[12](0), 0));
    a.L[112] = Eigen::Vector3f(a.L[12](1), -a.L[12](0), tmp);
    a.in_tri[112] = sub_in(tri_after(1, 1), tri_after(2, 1), a.L[112]) >= 0;

    tmp = -sub_in(tri_after(1, 1), tri_after(2, 1) , Eigen::Vector3f(a.L[12](1), -a.L[12](0), 0));
    a.L[221] = Eigen::Vector3f(a.L[12](1), -a.L[12](0), tmp);
    a.in_tri[221] = sub_in(tri_after(1, 0), tri_after(2, 0), a.L[221]) >= 0;

    preprocess_tri.push_back(a);
  }
  return 0;
}

int add_support(MeshPtr input_mesh, MeshPtr out_mesh, float alpha){
  ////////////////// TODO: read paper to find out what's this /////////////////
  double m_threshold = std::numeric_limits<double>::infinity();
  //////////////////////////////////////////////////////

  pcl::PointCloud<pcl::PointXYZ>::Ptr P(new pcl::PointCloud<pcl::PointXYZ>);  // recording every point's xyz data
  find_support_point(input_mesh, alpha, 1, P);

  std::vector<tri_data> preprocess_tri;
  preprocess(input_mesh, preprocess_tri);

  std::cerr<< "P size need to be supported:"<< P->size() << std::endl;
  SupportTree support_tree(P);

  sort(P -> points.begin(), P -> points.end(), sort_by_z);
  std::map<int, cone> C;  // recording every cone, key = index of P

  std::vector< std::pair<int, float> > P_v;  // main index list, first = index of P, second = z-coordinate
  for (size_t i = 0; i < P -> points.size(); i += 1){
    C[i] = cone(P -> points[i].x, P -> points[i].y, P -> points[i].z, alpha);
    P_v.push_back(std::pair<int, float>(i, P -> points[i].z));
  }
  sort(P_v.begin(), P_v.end(), sort_by_second);
  for (size_t i = 0; i < P_v.size(); i += 1){
    // support_tree.cloud -> push_back(P -> points[P_v[i].first]);
    support_tree.tree.push_back(tree_node(-1, -1, P_v[i].first, 1));
  }
  // std::cout<< "tree " << support_tree << std::endl;

  std::vector<std::pair <int, float> > S;  // candidates for intersetino point, first:index of P_v,second: distance

  std::map<int, cone> tmp_C;  // cones, key = index of P_v
  int current_point; // current index of P
  while(P_v.size() != 0){
    current_point = P_v.back().first;  // index of P
    P_v.pop_back();  // erase highest p, can slightly skip some if statement

    S.clear();
    tmp_C.clear();
    // cone-cone intersection
    for (size_t i = 0; i < P_v.size(); i += 1){
      cone tmp_cone;
      S.push_back(std::pair<int, double>(i, cone_intersect(C[current_point], C[P_v[i].first], tmp_cone)));
      tmp_C[i] = tmp_cone;
    }

    // cone-plate intersection
    S.push_back(std::pair<int, double>(-1, P -> points[current_point].z));

    // cone-mesh intersection, use -2 to indicate it's connected to mesh
    Eigen::Vector3f cm_point;
    S.push_back(std::pair<int, double>(-2, cone_mesh_intersect(C[current_point], input_mesh, preprocess_tri, cm_point)));

    // choose the right candidate
    std::pair<int, float> m(S[0]);  // first: index of P_v, second: distance
    for (size_t i = 0; i < S.size(); i += 1){
      if(S[i].second < m.second){
        m = S[i];
      }
      // std::cout<< "S:" << S[i].first << " " << S[i].second << std::endl;
    }
    if(m.second > m_threshold){
        // C.erase(current_point);
    }
    else{
      if(m.first >= 0){  // cone-cone
        // tmp_C[m.first]
        // std::cout<< "m.first " << m.first << std::endl;
        cone c(tmp_C[m.first]);
        C[P->points.size()] = c;

        P -> points.push_back(pcl::PointXYZ(c.pos[0], c.pos[1], c.pos[2]));
        std::pair<int, double> new_pv(P->size() - 1, c.pos[2]);
        // support_tree.cloud.push_back(P -> points.back())

        support_tree.tree.push_back(tree_node(current_point, P_v[m.first].first, new_pv.first, support_tree.tree[current_point].height + support_tree.tree[P_v[m.first].first].height));
        P_v.erase(P_v.begin() + m.first);

        std::vector< std::pair<int, float> >::iterator low;

        low = std::lower_bound (P_v.begin(), P_v.end(), new_pv, sort_by_second);
        P_v.insert(low, new_pv);
      }
      else if(m.first == -1){ // plate-cone
        // support_tree
        cone c;
        // P_v.push_back(std::pair<int, double>(P->size(),  ));
        P -> points.push_back(pcl::PointXYZ(P -> points[current_point].x, P -> points[current_point].y, 0));
        // C[P->size() ](c);
        support_tree.tree.push_back(tree_node(-3, current_point, P->size() - 1, support_tree.tree[current_point].height));
      }
      else if(m.first == -2){ // mesh-cone
        //////////////////// TODO ////////////////////////////////////////
        P -> points.push_back(pcl::PointXYZ(cm_point[0], cm_point[1], cm_point[2]));
        support_tree.tree.push_back(tree_node(-2, current_point, P->size() - 1, support_tree.tree[current_point].height));
        ////////////////////////////////////////////////////////////
      }
      else{
        std::cout<< "GG, this shouldn't  happen"<< std::endl;
        assert(false);
      }
    }
    // break;
  }

  // std::cout<< "tree " << support_tree << std::endl;
  // std::cout<< "support_tree.cloud " << support_tree.cloud -> size() << std::endl;
  support_tree.out_as_js();
  return 0;
}

double cone_intersect(cone a, cone b, cone &c){
  // computer the cone-cone intersection
  // usint inner division point
  // return distance between cone a's position and interseciton point

  float dz = b.pos[2] - a.pos[2];
  float dxy = sqrt(pow(a.pos[0] - b.pos[0], 2) + pow(a.pos[1] - b.pos[1], 2));
  if(dxy == 0){
    if(dz == 0){
      c = a;
      return 0;
    }
    return -1;
  }
  float h;
  // std::cout<< "tan " << tan(a.theta) << std::endl;
  // std::cout<< "dxy " << dxy << std::endl;
  // std::cout<< "dz " << dz << std::endl;
  h = (dxy / tan(a.theta) - dz) / 2;
  // std::cout<< "h:" << h << std::endl;

  c.pos[0] = ((h + dz) * a.pos[0] + h * b.pos[0]) / (h + dz + h),
  c.pos[1] = ((h + dz) * a.pos[1] + h * b.pos[1]) / (h + dz + h),
  c.pos[2] = a.pos[2] - h;
  c.theta = a.theta;
  return d_v3(a.pos, c.pos);
  // return sqrt(pow(a.pos[0] - c.pos[0], 2) + pow(a.pos[1] - c.pos[1], 2) + pow(a.pos[2] - c.pos[2], 2));
}

double cone_mesh_intersect(cone a, MeshPtr triangles, std::vector<tri_data> &preprocess_tri, Eigen::Vector3f &p){
  float tan_a = tan(a.theta);
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
  fromPCLPointCloud2(triangles->cloud, *cloud);

  float m = std::numeric_limits<float>::infinity();
  std::vector<int> main_list(3);
  main_list[0] = 12;
  main_list[1] = 13;
  main_list[2] = 23;

  float d;
  Eigen::Vector3f tmp_p;
  int index, tmp_sum;
  for (size_t i = 0; i < triangles->polygons.size(); i += 1){
    Eigen::Vector3f p_trans = preprocess_tri[i].tri_trans * a.pos;
    // Eigen::Vector3f p_yz(0, p_trans(1), p_trans(2));
    std::vector<int> record;
    for (size_t j = 0; j < 3; j += 1){
      if (bool(sub_in(p_trans(1), p_trans(2), preprocess_tri[i].L[main_list[j]]) >= 0) == preprocess_tri[i].in_tri[main_list[j]]){
        record.push_back(main_list[j]);
      }
    }
    if (record.size() == 3){  // in the triangle
      d = std::abs(p_trans(0));
      tmp_p = Eigen::Vector3f(0, p_trans(1), p_trans(2));
    }
    else if(record.size() == 2){ // nearst to another line
      tmp_sum = record[0] + record[1];
      if(tmp_sum == 12 + 13){
        index = 23;
      }
      else if(tmp_sum == 12 + 23){
        index = 13;
      }
      else if(tmp_sum == 13 + 23){
        index = 12;
      }
      float c_n = (preprocess_tri[i].L[index](1) * p_trans(1)+ preprocess_tri[i].L[index](0) * p_trans(2)) * -1;
      float a_sq_plus_b_sq = pow(preprocess_tri[i].L[index](0), 2) + pow(preprocess_tri[i].L[index](1), 2);
      tmp_p = Eigen::Vector3f(0,
        (-preprocess_tri[i].L[index](0) * preprocess_tri[i].L[index](2) - preprocess_tri[i].L[index](1) * c_n) / a_sq_plus_b_sq,
        (preprocess_tri[i].L[index](0) * c_n - preprocess_tri[i].L[index](1) * preprocess_tri[i].L[index](2)) / a_sq_plus_b_sq);
      d = d_v3(tmp_p, p_trans);
    }
    else if(record.size() == 1){// nearst to another point
      if(record[0] == 12){
        index = 3 - 1;
      }
      else if(record[0] == 13){
        index = 2 - 1;
      }
      else if(record[0] == 23){
        index = 1 - 1;
      }
      tmp_p = Eigen::Vector3f(preprocess_tri[i].tri_after(0, index), preprocess_tri[i].tri_after(1, index), preprocess_tri[i].tri_after(2, index));
      d = d_v3(tmp_p, p_trans);
      // line_point
    }
    else{
      // d+=1;
      std::cerr<< "i " << i << " "<< record.size() << std::endl;
      std::cerr<< "p_trans " << p_trans << std::endl;
      std::cerr<< "1 f\n " << preprocess_tri[i].L[main_list[0]] << std::endl;
      std::cerr<< "2 f\n " << preprocess_tri[i].L[main_list[1]] << std::endl;
      std::cerr<< "3 f\n " << preprocess_tri[i].L[main_list[2]] << std::endl;

      std::cerr<< "n 1\n " << sub_in(p_trans(1), p_trans(2), preprocess_tri[i].L[main_list[0]]) << std::endl;
      std::cerr<< "n 2\n " << bool(sub_in(p_trans(1), p_trans(2), preprocess_tri[i].L[main_list[1]]) >=0) << std::endl;
      std::cerr<< "n 3\n " << sub_in(p_trans(1), p_trans(2), preprocess_tri[i].L[main_list[2]]) << std::endl;

      std::cerr<< " a1\n " << preprocess_tri[i].in_tri[main_list[0]] << std::endl;
      std::cerr<< " a2\n " << preprocess_tri[i].in_tri[main_list[1]] << std::endl;
      std::cerr<< " a3\n " << preprocess_tri[i].in_tri[main_list[2]] << std::endl;
      if (bool(sub_in(p_trans(1), p_trans(2), preprocess_tri[i].L[main_list[1]]) >=0) == preprocess_tri[i].in_tri[main_list[1]])
      {
          std::cerr<< "ok " << std::endl;
      }
      exit(1);
    }
    tmp_p = preprocess_tri[i].tri_trans.inverse() * tmp_p;

    float h = a.pos[2] - tmp_p(2);
    if(h >= 0){
      if(tan_a * h >= sqrt(pow(tmp_p[0] - a.pos[0], 2) + pow(tmp_p[1] - a.pos[1], 2))){
        if(d < m){
          m = d;
          p = tmp_p;
          std::cerr<< "i " << i << " "<< record.size() << std::endl;
        }
      }
    }

  }
  std::cerr<< "m " << m << std::endl;
  std::cerr<< "tmp_p " << tmp_p << std::endl;
  std::cerr<< "p " << p << std::endl;

  return m;
}

int find_support_point(MeshPtr triangles, float alpha, float sample_rate, pcl::PointCloud<pcl::PointXYZ>::Ptr P){
  // ref: http://hpcg.purdue.edu/bbenes/papers/Vanek14SGP.pdf
  // MeshPtr triangles[in]: input stl
  // float alpha[in]: angle that >= alpha need to be supported
  // float sample_rate[in]: sample reate (grid)
  // P[out]: out put the points that need to be supported

  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
  fromPCLPointCloud2(triangles->cloud, *cloud);
  std::cerr << "input points " << cloud -> size() << std::endl;
  std::cerr << "input faces " << triangles->polygons.size() << std::endl;
  // std::vector<float> normal_p;
  // std::vector<float> normal_vertical;
  // normal_vertical.push_back(0);
  // normal_vertical.push_back(0);
  // normal_vertical.push_back(1);
  float cos_alpha = cos((M_PI / 2.0) - alpha);
  float a[3], b[3], normal_p[3];
  float la, lb;
  const float normal_vertical[3] = {0, 0, -1};
  std::vector<int> rec;
  for (size_t i = 0; i < triangles->polygons.size(); i += 1){
  // for (size_t i = 0; i < 1; i += 1){

    a[0] = (*cloud)[(triangles->polygons[i]).vertices[1]].x - (*cloud)[(triangles->polygons[i]).vertices[0]].x;
    a[1] = (*cloud)[(triangles->polygons[i]).vertices[1]].y - (*cloud)[(triangles->polygons[i]).vertices[0]].y;
    a[2] = (*cloud)[(triangles->polygons[i]).vertices[1]].z - (*cloud)[(triangles->polygons[i]).vertices[0]].z;

    b[0] = (*cloud)[(triangles->polygons[i]).vertices[2]].x - (*cloud)[(triangles->polygons[i]).vertices[0]].x;
    b[1] = (*cloud)[(triangles->polygons[i]).vertices[2]].y - (*cloud)[(triangles->polygons[i]).vertices[0]].y;
    b[2] = (*cloud)[(triangles->polygons[i]).vertices[2]].z - (*cloud)[(triangles->polygons[i]).vertices[0]].z;

    normal_p[0] = a[1] * b[2] - a[2] * b[1];
    normal_p[1] = a[2] * b[0] - a[0] * b[2];
    normal_p[2] = a[0] * b[1] - a[1] * b[0];

    // std::cout << normal_p[2] << std::endl;
    float cos_theta = (normal_p[0] * normal_vertical[0] + normal_p[1] * normal_vertical[1] + normal_p[2] * normal_vertical[2]) / (sqrt(normal_p[0] * normal_p[0] + normal_p[1] * normal_p[1] + normal_p[2] * normal_p[2]) * sqrt(normal_vertical[0] * normal_vertical[0] + normal_vertical[1] * normal_vertical[1] + normal_vertical[2] * normal_vertical[2]));
    // std::cout << cos_theta << std::endl;
    // faces that need to be supported
    if(cos_theta >= cos_alpha){  //cosine, larger angle, smaller cosine
      rec.push_back(i);
      // std::cout << "> "<<i << std::endl;
      b[0] = (*cloud)[(triangles->polygons[i]).vertices[2]].x - (*cloud)[(triangles->polygons[i]).vertices[1]].x;
      b[1] = (*cloud)[(triangles->polygons[i]).vertices[2]].y - (*cloud)[(triangles->polygons[i]).vertices[1]].y;
      b[2] = (*cloud)[(triangles->polygons[i]).vertices[2]].z - (*cloud)[(triangles->polygons[i]).vertices[1]].z;
      la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
      lb = sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
      int ia = (int)(la / (sample_rate / 10.));
      int ib = (int)(lb / (sample_rate / 10.));
      // std::cout<< "ia:"<< ia << " " << ib << std::endl;

      for (size_t j = 0; j < ia; j += 1){
        for (size_t k = 0; k < (float)j / ia * ib; k += 1){
          pcl::PointXYZ point;
          point.x = (*cloud)[(triangles->polygons[i]).vertices[0]].x + a[0] * j / ia + b[0] * k / ib;
          point.y = (*cloud)[(triangles->polygons[i]).vertices[0]].y + a[1] * j / ia + b[1] * k / ib;
          point.z = (*cloud)[(triangles->polygons[i]).vertices[0]].z + a[2] * j / ia + b[2] * k / ib;
          if(point.z != 0){
            P -> push_back(point);
          }
        }
      }
    }
  }
  std::cerr<< "face need:"<< rec.size() << std::endl;
  std::cerr<< "P size before:"<< P->size() << std::endl;
  pcl::VoxelGrid<pcl::PointXYZ> grid;
  grid.setLeafSize(sample_rate, sample_rate, sample_rate);
  grid.setInputCloud(P);
  grid.filter(*P);
  std::cerr<< "P size after:"<< P->size() << std::endl;
  ////////////////////fake code //////////////////////////////


  pcl::io::savePCDFileASCII ("tmp.pcd", *P + *cloud);
  ////////////////////////////////////////////////////////////

  return 0;
}
