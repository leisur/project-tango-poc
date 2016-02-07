/*
 * Copyright 2014 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "tango-augmented-reality/chisel_mesh.h"
#include <tango-gl/shaders.h>

namespace tango_augmented_reality {
    ChiselMesh::ChiselMesh() {
        render_mode_ = GL_TRIANGLES;
        SetShader();

        chunkSize = 16;
        truncationDistScale = 8.0;
        weighting = 1.0;
        enableCarving = true;
        carvingDistance = 0.5;
        chunkResolution = 0.06;
        farClipping = 2.0;
        rayTruncation = 0.5;

        chiselMap = chisel::ChiselPtr(new chisel::Chisel(Eigen::Vector3i(chunkSize, chunkSize, chunkSize), chunkResolution, false));
        chisel::TruncatorPtr truncator(new chisel::ConstantTruncator(truncationDistScale));
        chisel::ConstantWeighterPtr weighter(new chisel::ConstantWeighter(weighting));

        chisel::Vec3List centroids;
        projectionIntegrator = chisel::ProjectionIntegrator(truncator, weighter, carvingDistance, enableCarving, centroids);
        projectionIntegrator.SetCentroids(chiselMap->GetChunkManager().GetCentroids());
        LOGI("chisel container was created in native environment");



    }

    void ChiselMesh::addPoints(std::vector<float> vertices, glm::mat4 transformation) {

        // move jfloatArray vertices to Chisel PointCloud
        lastPointCloud->Clear();
        int vertexCount = vertices.size() / 3;
        LOGE("got %d points from as pointcloud data", vertexCount / 3);
        for (int i = 0; i < vertexCount; ++i) {
            chisel::Vec3 vec3(vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2]);
            lastPointCloud->AddPoint(vec3);
        }

        chisel::Transform extrinsic = chisel::Transform();
        for (int j = 0; j < 4; ++j) {
            for (int k = 0; k < 4; ++k) {
                extrinsic(j, k) = transformation[j][k];
            }
        }

        chiselMap->IntegratePointCloud(
                projectionIntegrator,
                *lastPointCloud,
                extrinsic,
                rayTruncation,
                farClipping
        );

    }

    void ChiselMesh::updateVertices() {
        chiselMap->UpdateMeshes();
        LOGI("Generating Mesh ...");
        chisel::MeshMap meshMap = chiselMap->GetChunkManager().GetAllMeshes();
        LOGI("Map with %d items", meshMap.size());

        std::vector <GLfloat> mesh;
        std::vector <GLushort> indices;
        for (const std::pair <chisel::ChunkID, chisel::MeshPtr> &meshes : meshMap) {
            for (size_t &index: meshes.second->indices) {
                mesh.push_back(meshes.second->vertices[index](0));
                mesh.push_back(meshes.second->vertices[index](1));
                mesh.push_back(meshes.second->vertices[index](2));
                indices.push_back(index*3);
                indices.push_back(index*3+1);
                indices.push_back(index*3+2);
            }
        }
        LOGI("Got %d polygons", mesh.size() / 3);
        SetVertices(mesh, indices);
    }

    ChiselMesh::ChiselMesh(GLenum render_mode) {
        render_mode_ = render_mode;
    }

    void ChiselMesh::SetShader() {
        tango_gl::DrawableObject::SetShader();
        // Default mode set to no lighting.
        is_lighting_on_ = false;
        // Default mode set to without bounding box detection.
        is_bounding_box_on_ = false;
    }

    void ChiselMesh::SetShader(bool is_lighting_on) {
        if (is_lighting_on) {
            shader_program_ =
                    tango_gl::util::CreateProgram(tango_gl::shaders::GetShadedVertexShader().c_str(),
                                                  tango_gl::shaders::GetBasicFragmentShader().c_str());
            if (!shader_program_) {
                LOGE("Could not create program.");
            }
            uniform_mvp_mat_ = glGetUniformLocation(shader_program_, "mvp");
            uniform_mv_mat_ = glGetUniformLocation(shader_program_, "mv");
            uniform_light_vec_ = glGetUniformLocation(shader_program_, "lightVec");
            uniform_color_ = glGetUniformLocation(shader_program_, "color");

            attrib_vertices_ = glGetAttribLocation(shader_program_, "vertex");
            attrib_normals_ = glGetAttribLocation(shader_program_, "normal");
            is_lighting_on_ = true;
            // Set a defualt direction for directional light.
            light_direction_ = glm::vec3(-1.0f, -3.0f, -1.0f);
            light_direction_ = glm::normalize(light_direction_);
        } else {
            SetShader();
        }
    }

    void ChiselMesh::SetBoundingBox() {
        // Traverse all the vertices to define an axis-aligned
        // bounding box for this ChiselMesh, needs to be called after SetVertices().
        if (vertices_.size() == 0) {
            LOGE("Please set up vertices first!");
            return;
        }
        is_bounding_box_on_ = true;
        bounding_box_ = new tango_gl::BoundingBox(vertices_);
    }

    void ChiselMesh::SetLightDirection(const glm::vec3 &light_direction) {
        light_direction_ = light_direction;
    }

    bool ChiselMesh::IsIntersecting(const tango_gl::Segment &segment) {
        // If there is no bounding box defined based on all vertices,
        // we can not calculate intersection.
        if (!is_bounding_box_on_) {
            LOGE("ChiselMesh::IsIntersecting, bounding box is not available.");
            return false;
        }
        return bounding_box_->IsIntersecting(segment, GetRotation(),
                                             GetTransformationMatrix());
    }

    void ChiselMesh::Render(const glm::mat4 &projection_mat,
                            const glm::mat4 &view_mat) const {
        glUseProgram(shader_program_);
        glm::mat4 model_mat = GetTransformationMatrix();
        glm::mat4 mv_mat = view_mat * model_mat;
        glm::mat4 mvp_mat = projection_mat * mv_mat;
        glUniformMatrix4fv(uniform_mvp_mat_, 1, GL_FALSE, glm::value_ptr(mvp_mat));
        glUniform4f(uniform_color_, red_, green_, blue_, alpha_);

        if (is_lighting_on_) {
            glUniformMatrix4fv(uniform_mv_mat_, 1, GL_FALSE, glm::value_ptr(mv_mat));

            glEnableVertexAttribArray(attrib_normals_);
            glVertexAttribPointer(attrib_normals_, 3, GL_FLOAT, GL_FALSE,
                                  3 * sizeof(GLfloat), &normals_[0]);
            glm::vec3 light_direction = glm::mat3(view_mat) * light_direction_;
            glUniform3fv(uniform_light_vec_, 1, glm::value_ptr(light_direction));
        }

        glEnableVertexAttribArray(attrib_vertices_);

        if (!indices_.empty()) {
            glVertexAttribPointer(attrib_vertices_, 3, GL_FLOAT, GL_FALSE,
                                  3 * sizeof(GLfloat), vertices_.data());
            glDrawElements(render_mode_, indices_.size(), GL_UNSIGNED_SHORT,
                           indices_.data());
        } else {
            glVertexAttribPointer(attrib_vertices_, 3, GL_FLOAT, GL_FALSE,
                                  3 * sizeof(GLfloat), &vertices_[0]);
            glDrawArrays(render_mode_, 0, vertices_.size() / 3);
        }

        glDisableVertexAttribArray(attrib_vertices_);
        if (is_lighting_on_) {
            glDisableVertexAttribArray(attrib_normals_);
        }
        glUseProgram(0);
    }
}  // namespace tango_augmented_reality