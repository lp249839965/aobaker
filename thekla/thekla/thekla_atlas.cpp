
#include "thekla_atlas.h"

#include <cfloat>
#include <cstdio>

#include "nvmesh/halfedge/Edge.h"
#include "nvmesh/halfedge/Mesh.h"
#include "nvmesh/halfedge/Face.h"
#include "nvmesh/halfedge/Vertex.h"
#include "nvmesh/param/Atlas.h"
#include "nvmesh/raster/Raster.h"

#include "nvmath/Vector.inl"

#include "nvcore/Array.inl"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

using namespace Thekla;
using namespace nv;


inline Atlas_Output_Mesh * set_error(Atlas_Error * error, Atlas_Error code) {
    if (error) *error = code;
    return NULL;
}



static void input_to_mesh(const Atlas_Input_Mesh * input, HalfEdge::Mesh * mesh, Atlas_Error * error) {

    Array<uint> canonicalMap;
    canonicalMap.reserve(input->vertex_count);

    for (int i = 0; i < input->vertex_count; i++) {
        const Atlas_Input_Vertex & input_vertex = input->vertex_array[i];
        const float * pos = input_vertex.position;
        const float * nor = input_vertex.normal;
        const float * tex = input_vertex.uv;

        HalfEdge::Vertex * vertex = mesh->addVertex(Vector3(pos[0], pos[1], pos[2]));
        vertex->nor.set(nor[0], nor[1], nor[2]);
        vertex->tex.set(tex[0], tex[1]);

        canonicalMap.append(input_vertex.first_colocal);
    }

    mesh->linkColocalsWithCanonicalMap(canonicalMap);


    const int face_count = input->face_count;

    int non_manifold_faces = 0;
    for (int i = 0; i < face_count; i++) {
        const Atlas_Input_Face & input_face = input->face_array[i];

        int v0 = input_face.vertex_index[0];
        int v1 = input_face.vertex_index[1];
        int v2 = input_face.vertex_index[2];

        HalfEdge::Face * face = mesh->addFace(v0, v1, v2);
        if (face != NULL) {
            face->material = input_face.material_index;
        }
        else {
            non_manifold_faces++;
        }
    }

    mesh->linkBoundary();

    if (non_manifold_faces != 0 && error != NULL) {
        *error = Atlas_Error_Invalid_Mesh_Non_Manifold;
    }
}

static Atlas_Output_Mesh * mesh_atlas_to_output(const HalfEdge::Mesh * mesh, const Atlas & atlas, Atlas_Error * error) {

    Atlas_Output_Mesh * output = new Atlas_Output_Mesh;

    // Allocate vertices.
    const int vertex_count = atlas.vertexCount();
    output->vertex_count = vertex_count;
    output->vertex_array = new Atlas_Output_Vertex[vertex_count];

    // Output vertices.
    const int chart_count = atlas.chartCount();
    for (int i = 0; i < chart_count; i++) {
        const Chart * chart = atlas.chartAt(i);
        uint vertexOffset = atlas.vertexCountBeforeChartAt(i);

        const uint chart_vertex_count = chart->vertexCount();
        for (uint v = 0; v < chart_vertex_count; v++) {
            Atlas_Output_Vertex & output_vertex = output->vertex_array[vertexOffset + v]; 

            uint original_vertex = chart->mapChartVertexToOriginalVertex(v);
            output_vertex.xref = original_vertex;

            Vector2 uv = chart->chartMesh()->vertexAt(v)->tex;
            output_vertex.uv[0] = uv.x;
            output_vertex.uv[1] = uv.y;
        }
    }

    const int face_count = mesh->faceCount();
    output->index_count = face_count * 3;
    output->index_array = new int[face_count * 3];

        // Set face indices.
    for (int f = 0; f < face_count; f++) {
        uint c = atlas.faceChartAt(f);
        uint i = atlas.faceIndexWithinChartAt(f);
        uint vertexOffset = atlas.vertexCountBeforeChartAt(c);

        const Chart * chart = atlas.chartAt(c);
        nvDebugCheck(chart->faceAt(i) == f);

        const HalfEdge::Face * face = chart->chartMesh()->faceAt(i);
        const HalfEdge::Edge * edge = face->edge;

        output->index_array[3*f+0] = vertexOffset + edge->vertex->id;
        output->index_array[3*f+1] = vertexOffset + edge->next->vertex->id;
        output->index_array[3*f+2] = vertexOffset + edge->next->next->vertex->id;
    }

    // @@ Init these!
    *error = Atlas_Error_Not_Implemented;
    output->atlas_width = atlas.width();
    output->atlas_height = atlas.height();

    return output;
}


void Thekla::atlas_set_default_options(Atlas_Options * options) {
    if (options != NULL) {
        options->charter = Atlas_Charter_Default;
        options->charter_options.witness.proxy_fit_metric_weight = 2.0f;
        options->charter_options.witness.roundness_metric_weight = 0.01f;
        options->charter_options.witness.straightness_metric_weight = 6.0f;
        options->charter_options.witness.normal_seam_metric_weight = 4.0f;
        options->charter_options.witness.texture_seam_metric_weight = 0.5f;
        options->charter_options.witness.max_chart_area = FLT_MAX;
        options->charter_options.witness.max_boundary_length = FLT_MAX;
        options->mapper = Atlas_Mapper_Default;
        options->packer = Atlas_Packer_Default;
        options->packer_options.witness.packing_quality = 1;
        options->packer_options.witness.texel_area = 8;
        options->packer_options.witness.texel_padding = 1;
    }
}


Atlas_Output_Mesh * Thekla::atlas_generate(const Atlas_Input_Mesh * input, const Atlas_Options * options, Atlas_Error * error) {
    // Validate args.
    if (input == NULL || options == NULL || error == NULL) return set_error(error, Atlas_Error_Invalid_Args);

    // Validate options.
    if (options->charter != Atlas_Charter_Witness) {
        return set_error(error, Atlas_Error_Invalid_Options);
    }
    if (options->charter == Atlas_Charter_Witness) {
        // @@ Validate input options!
    }

    if (options->mapper != Atlas_Mapper_LSCM) {
        return set_error(error, Atlas_Error_Invalid_Options);
    }
    if (options->mapper == Atlas_Mapper_LSCM) {
        // No options.
    }

    if (options->packer != Atlas_Packer_Witness) {
        return set_error(error, Atlas_Error_Invalid_Options);
    }
    if (options->packer == Atlas_Packer_Witness) {
        // @@ Validate input options!
    }

    // Validate input mesh.
    for (int i = 0; i < input->face_count; i++) {
        int v0 = input->face_array[i].vertex_index[0];
        int v1 = input->face_array[i].vertex_index[1];
        int v2 = input->face_array[i].vertex_index[2];

        if (v0 < 0 || v0 >= input->vertex_count || 
            v1 < 0 || v1 >= input->vertex_count || 
            v2 < 0 || v2 >= input->vertex_count)
        {
            return set_error(error, Atlas_Error_Invalid_Mesh);
        }
    }


    // Build half edge mesh.
    AutoPtr<HalfEdge::Mesh> mesh(new HalfEdge::Mesh);

    input_to_mesh(input, mesh.ptr(), error);

    if (*error == Atlas_Error_Invalid_Mesh) {
        return NULL;
    }


    // Charter.
    Atlas atlas(mesh.ptr());
    
    if (options->charter == Atlas_Charter_Extract) {
        return set_error(error, Atlas_Error_Not_Implemented);
    }
    else if (options->charter == Atlas_Charter_Witness) {
        SegmentationSettings segmentation_settings;
        segmentation_settings.proxyFitMetricWeight = options->charter_options.witness.proxy_fit_metric_weight;
        segmentation_settings.roundnessMetricWeight = options->charter_options.witness.roundness_metric_weight;
        segmentation_settings.straightnessMetricWeight = options->charter_options.witness.straightness_metric_weight;
        segmentation_settings.normalSeamMetricWeight = options->charter_options.witness.normal_seam_metric_weight;
        segmentation_settings.textureSeamMetricWeight = options->charter_options.witness.texture_seam_metric_weight;
        segmentation_settings.maxChartArea = options->charter_options.witness.max_chart_area;
        segmentation_settings.maxBoundaryLength = options->charter_options.witness.max_boundary_length;

        atlas.computeCharts(segmentation_settings);
    }


    // Mapper.
    if (options->mapper == Atlas_Mapper_LSCM) {
        atlas.parameterizeCharts();
    }


    // Packer.
    if (options->packer == Atlas_Packer_Witness) {
        int packing_quality = options->packer_options.witness.packing_quality;
        float texel_area = options->packer_options.witness.texel_area;
        int texel_padding = options->packer_options.witness.texel_padding;

        /*float utilization =*/ atlas.packCharts(packing_quality, texel_area, texel_padding);
    }


    // Build output mesh.
    return mesh_atlas_to_output(mesh.ptr(), atlas, error);
}

struct PngShaderData {
    int width;
    Vector3 objspace[3];
    float* floats;
    unsigned char* data;
    unsigned char color[4];
    float fpcolor[4];
};

static bool pngAtlasCallback(
    void * param, int x, int y, Vector3::Arg tex,
    Vector3::Arg b, Vector3::Arg c, float area)
{
    unsigned char* colors = ((PngShaderData*) param)->data;
    Vector3* objspace = ((PngShaderData*) param)->objspace;
    int width = ((PngShaderData*) param)->width;
    int loc = x * 3 + y * 3 * width;
    Vector3 color = objspace[0] * tex.x +
        objspace[1] * tex.y +
        objspace[2] * tex.z;
    colors[loc++] = color.x * 255;
    colors[loc++] = color.y * 255;
    colors[loc++] = color.z * 255;
    return true;
}

static bool floatAtlasCallback(
    void * param, int x, int y, Vector3::Arg tex,
    Vector3::Arg b, Vector3::Arg c, float area)
{
    float* floats = ((PngShaderData*) param)->floats;
    Vector3* objspace = ((PngShaderData*) param)->objspace;
    int width = ((PngShaderData*) param)->width;
    int loc = x * 3 + y * 3 * width;
    Vector3 color = objspace[0] * tex.x +
        objspace[1] * tex.y +
        objspace[2] * tex.z;
    floats[loc++] = color.x;
    floats[loc++] = color.y;
    floats[loc++] = color.z;
    return true;
}

static bool pngSolidCallback(
    void * param, int x, int y, Vector3::Arg tex,
    Vector3::Arg b, Vector3::Arg c, float area)
{
    unsigned char* colors = ((PngShaderData*) param)->data;
    unsigned char* color = ((PngShaderData*) param)->color;
    int width = ((PngShaderData*) param)->width;
    int loc = x * 3 + y * 3 * width;
    colors[loc++] = color[0];
    colors[loc++] = color[1];
    colors[loc++] = color[2];
    return true;
}

static bool floatSolidCallback(
    void * param, int x, int y, Vector3::Arg tex,
    Vector3::Arg b, Vector3::Arg c, float area)
{
    float* floats = ((PngShaderData*) param)->floats;
    float* fpcolor = ((PngShaderData*) param)->fpcolor;
    int width = ((PngShaderData*) param)->width;
    int loc = x * 3 + y * 3 * width;
    floats[loc++] = fpcolor[0];
    floats[loc++] = fpcolor[1];
    floats[loc++] = fpcolor[2];
    return true;
}

void Thekla::atlas_dump(const Atlas_Output_Mesh * atlas_mesh, const Atlas_Input_Mesh * obj_mesh) {

    // Replace uv's in the original mesh.
    for (int nvert = 0; nvert < atlas_mesh->vertex_count; nvert++) {
        int srcvert = atlas_mesh->vertex_array[nvert].xref;
        float u = atlas_mesh->vertex_array[nvert].uv[0];
        float v = atlas_mesh->vertex_array[nvert].uv[1];
        obj_mesh->vertex_array[srcvert].uv[0] = u;
        obj_mesh->vertex_array[srcvert].uv[1] = v;
    }

    // Dump out the mutated mesh in simplified form and compute the AABB.
    printf("Writing modified.obj...\n");
    FILE* outobj = fopen("modified.obj", "wt");
    float uscale = 1.f / atlas_mesh->atlas_width;
    float vscale = 1.f / atlas_mesh->atlas_height;
    Vector3 minp(FLT_MAX);
    Vector3 maxp(FLT_MIN);
    for (int nvert = 0; nvert < obj_mesh->vertex_count; nvert++) {
        const Atlas_Input_Vertex& vert = obj_mesh->vertex_array[nvert];
        minp.x = nv::min(vert.position[0], minp.x);
        minp.y = nv::min(vert.position[1], minp.y);
        minp.z = nv::min(vert.position[2], minp.z);
        maxp.x = nv::max(vert.position[0], maxp.x);
        maxp.y = nv::max(vert.position[1], maxp.y);
        maxp.z = nv::max(vert.position[2], maxp.z);
        fprintf(outobj, "v %f %f %f\n", vert.position[0], vert.position[1], vert.position[2]);
        fprintf(outobj, "vt %f %f\n", vert.uv[0] * uscale, 1 - vert.uv[1] * vscale);
    }
    for (int nface = 0; nface < obj_mesh->face_count; nface++) {
        const Atlas_Input_Face& face = obj_mesh->face_array[nface];
        fprintf(outobj, "f %d/%d %d/%d %d/%d\n",
            face.vertex_index[0]+1, face.vertex_index[0]+1,
            face.vertex_index[1]+1, face.vertex_index[1]+1,
            face.vertex_index[2]+1, face.vertex_index[2]+1);
    }
    fclose(outobj);

    // Create a PNG file representing the charts, with color representing the
    // approximate world-space position of each source vertex.
    int width = atlas_mesh->atlas_width;
    int height = atlas_mesh->atlas_height;
    const Vector2 extents(width, height);
    unsigned char* colors = (unsigned char*) calloc(width * height * 3, 1);
    float* floats = (float*) calloc(width * height * sizeof(float) * 3, 1);
    const int* patlasIndex = atlas_mesh->index_array;
    Vector2 triverts[3];
    PngShaderData png;
    png.width = width;
    png.data = colors;
    png.floats = floats;
    Vector3 objextent = maxp - minp;
    Vector3 offset = -minp;
    float objmextent = nv::max(nv::max(objextent.x, objextent.y), objextent.z);
    float scale = 1.0f / objmextent;
    for (int nface = 0; nface < atlas_mesh->index_count / 3; nface++) {
        Atlas_Output_Vertex& a = atlas_mesh->vertex_array[*patlasIndex++];
        Atlas_Output_Vertex& b = atlas_mesh->vertex_array[*patlasIndex++];
        Atlas_Output_Vertex& c = atlas_mesh->vertex_array[*patlasIndex++];
        Atlas_Input_Vertex& i = obj_mesh->vertex_array[a.xref];
        Atlas_Input_Vertex& j = obj_mesh->vertex_array[b.xref];
        Atlas_Input_Vertex& k = obj_mesh->vertex_array[c.xref];
        triverts[0].x = a.uv[0];
        triverts[0].y = a.uv[1];
        triverts[1].x = b.uv[0];
        triverts[1].y = b.uv[1];
        triverts[2].x = c.uv[0];
        triverts[2].y = c.uv[1];

        png.objspace[0].x = (i.position[0] + offset.x) * scale;
        png.objspace[0].y = (i.position[1] + offset.y) * scale;
        png.objspace[0].z = (i.position[2] + offset.z) * scale;
        png.objspace[1].x = (j.position[0] + offset.x) * scale;
        png.objspace[1].y = (j.position[1] + offset.y) * scale;
        png.objspace[1].z = (j.position[2] + offset.z) * scale;
        png.objspace[2].x = (k.position[0] + offset.x) * scale;
        png.objspace[2].y = (k.position[1] + offset.y) * scale;
        png.objspace[2].z = (k.position[2] + offset.z) * scale;
        Raster::drawTriangle(true, extents, true, triverts, pngAtlasCallback, &png);

        png.objspace[0].x = i.position[0];
        png.objspace[0].y = i.position[1];
        png.objspace[0].z = i.position[2];
        png.objspace[1].x = j.position[0];
        png.objspace[1].y = j.position[1];
        png.objspace[1].z = j.position[2];
        png.objspace[2].x = k.position[0];
        png.objspace[2].y = k.position[1];
        png.objspace[2].z = k.position[2];
        Raster::drawTriangle(true, extents, true, triverts, floatAtlasCallback, &png);
    }
    printf("Writing object_coords.png...\n");
    stbi_write_png("object_coords.png", width, height, 3, colors, 0);
    printf("Writing object_coords.bin...\n");
    FILE* objbin = fopen("object_coords.bin", "wb");
    fwrite(&width, 1, 4, objbin);
    fwrite(&height, 1, 4, objbin);
    fwrite(floats, 1, width * height * 4 * 3, objbin);
    fclose(objbin);

    // Create a PNG file representing facet normals.
    memset(floats, 0, width * height * sizeof(float) * 3);
    patlasIndex = atlas_mesh->index_array;
    for (int nface = 0; nface < atlas_mesh->index_count / 3; nface++) {
        Atlas_Output_Vertex& a = atlas_mesh->vertex_array[*patlasIndex++];
        Atlas_Output_Vertex& b = atlas_mesh->vertex_array[*patlasIndex++];
        Atlas_Output_Vertex& c = atlas_mesh->vertex_array[*patlasIndex++];
        Atlas_Input_Vertex& i = obj_mesh->vertex_array[a.xref];
        Atlas_Input_Vertex& j = obj_mesh->vertex_array[b.xref];
        Atlas_Input_Vertex& k = obj_mesh->vertex_array[c.xref];
        triverts[0].x = a.uv[0];
        triverts[0].y = a.uv[1];
        triverts[1].x = b.uv[0];
        triverts[1].y = b.uv[1];
        triverts[2].x = c.uv[0];
        triverts[2].y = c.uv[1];
        png.objspace[0].x = i.position[0];
        png.objspace[0].y = i.position[1];
        png.objspace[0].z = i.position[2];
        png.objspace[1].x = j.position[0];
        png.objspace[1].y = j.position[1];
        png.objspace[1].z = j.position[2];
        png.objspace[2].x = k.position[0];
        png.objspace[2].y = k.position[1];
        png.objspace[2].z = k.position[2];
        Vector3 A = png.objspace[1] - png.objspace[0];
        Vector3 B = png.objspace[2] - png.objspace[0];
        Vector3 N = 0.5f * (normalize(cross(A, B)) + Vector3(1, 1, 1));
        png.color[0] = N.x * 255;
        png.color[1] = N.y * 255;
        png.color[2] = N.z * 255;
        Raster::drawTriangle(true, extents, true, triverts, pngSolidCallback, &png);

        png.fpcolor[0] = N.x;
        png.fpcolor[1] = N.y;
        png.fpcolor[2] = N.z;
        Raster::drawTriangle(true, extents, true, triverts, floatSolidCallback, &png);
    }
    printf("Writing facet_normals.png...\n");
    stbi_write_png("facet_normals.png", width, height, 3, colors, 0);
    printf("Writing facet_normals.bin...\n");
    FILE* normbin = fopen("facet_normals.bin", "wb");
    fwrite(&width, 1, 4, normbin);
    fwrite(&height, 1, 4, normbin);
    fwrite(floats, 1, width * height * sizeof(float) * 3, normbin);
    fclose(normbin);

    free(colors);
    free(floats);

    printf("Writing object_coords.bin...\n");
}


void Thekla::atlas_free(Atlas_Output_Mesh * output) {
    if (output != NULL) {
        delete [] output->vertex_array;
        delete [] output->index_array;
        delete output;
    }
}
