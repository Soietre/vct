#include "Application.h"

#include <Graphics/opengl.h>
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

#include <iostream>
#include <vector>
#include <memory>

#include "Graphics/GLHelper.h"
#include "Graphics/GLShaderProgram.h"
#include "Graphics/GLQuad.h"
#include "Graphics/GLTimer.h"

#include "Input/Keyboard.h"
#include "Input/Mouse.h"

#include "Overlay.h"
#include "Camera.h"
#include "Scene.h"

#include "common.h"

#define SHADOWMAP_WIDTH 4096
#define SHADOWMAP_HEIGHT 4096

#define RSM 1

void view2DTexture(GLuint texture);

void Application::init() {
    // Setup for OpenGL
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(0.5294f, 0.8078f, 0.9216f, 1.0f);

    // Setup framebuffers
    shadowmapFBO.bind();
    glm::vec4 borderColor{ 1.0f };
    shadowmapFBO.attachTexture(
        GL_DEPTH_ATTACHMENT, GL_DEPTH_COMPONENT,
        SHADOWMAP_WIDTH, SHADOWMAP_HEIGHT,
        GL_DEPTH_COMPONENT, GL_FLOAT,
        GL_LINEAR, GL_LINEAR,
        GL_CLAMP_TO_BORDER, GL_CLAMP_TO_BORDER,
        &borderColor
    );
#if RSM
    shadowmapFBO.attachTexture(
        GL_COLOR_ATTACHMENT0, GL_RGB8, SHADOWMAP_WIDTH, SHADOWMAP_HEIGHT, GL_RGB, GL_UNSIGNED_BYTE
    );
    shadowmapFBO.attachTexture(
        GL_COLOR_ATTACHMENT1, GL_RGB8, SHADOWMAP_WIDTH, SHADOWMAP_HEIGHT, GL_RGB, GL_UNSIGNED_BYTE
    );
    GLuint attachments[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, attachments);
#else
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
#endif
    if (shadowmapFBO.getStatus() != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("shadowmapFBO not created successfully");
    }
    shadowmapFBO.unbind();

    // Create shaders
    program.attachAndLink({SHADER_DIR "phong.vert", SHADER_DIR "phong.frag"});
    program.setObjectLabel("Phong");
    voxelProgram.attachAndLink({SHADER_DIR "voxelize.vert", SHADER_DIR "voxelize.frag", SHADER_DIR "voxelize.geom"});
    voxelProgram.setObjectLabel("Voxelize");
#if RSM
    shadowmapProgram.attachAndLink({SHADER_DIR "simple.vert", SHADER_DIR "reflectiveShadowMap.frag"});
    shadowmapProgram.setObjectLabel("RSM");
#else
    shadowmapProgram.attachAndLink({SHADER_DIR "simple.vert", SHADER_DIR "empty.frag"});
    shadowmapProgram.setObjectLabel("Shadowmap");
#endif
    injectRadianceProgram.attachAndLink({SHADER_DIR "injectRadiance.comp"});
    injectRadianceProgram.setObjectLabel("Inject Radiance");
    temporalRadianceFilterProgram.attachAndLink({SHADER_DIR "temporalRadianceFilter.comp"});
    temporalRadianceFilterProgram.setObjectLabel("Temporal Radiance Filter");
    mipmapProgram.attachAndLink({SHADER_DIR "filterRadiance.comp"});
    mipmapProgram.setObjectLabel("Filter Radiance");
    ditherProgram.attachAndLink({SHADER_DIR "simple.vert", SHADER_DIR "dither.frag"});
    ditherProgram.setObjectLabel("Dither");

    // Create scene
    scene = std::make_unique<Scene>();

    StaticMeshActor sponza {RESOURCE_DIR "sponza/sponza_dds.obj"};
    // StaticMeshActor sponza {RESOURCE_DIR "sponza/sponza_pbr.obj"}, nanosuit {RESOURCE_DIR "nanosuit/nanosuit.obj"};
    sponza.transform.setScale(glm::vec3(0.01f));
    scene->addActor(std::make_shared<StaticMeshActor>(sponza));

    // nanosuit.transform.setScale(glm::vec3(0.25f));
    // nanosuit.controller = new LambdaActorController([](Actor &actor, float dt, float time) {
    // 	const float speedMultiplier = 1.0f;
    // 	// actor.transform.setPosition(actor.transform.getPosition() + glm::vec3(0.0f, glm::sin(time), 0.0f));
    // 	actor.transform.setScale(glm::vec3(0.25f + 0.05f * glm::sin(speedMultiplier * time)));
    // 	actor.transform.setPosition(glm::vec3(glm::cos(speedMultiplier * time), 0.0f, glm::sin(speedMultiplier * time)));
    // 	actor.transform.rotate(speedMultiplier * dt, glm::vec3(0.0f, 1.0f, 0.0f));
    // });
    // scene->addActor(std::make_shared<StaticMeshActor>(nanosuit));

    // StaticMeshActor nanosuit2 {RESOURCE_DIR "nanosuit/nanosuit.obj"};
    // nanosuit2.transform.setScale(glm::vec3(0.2f));
    // nanosuit2.controller = new LambdaActorController([](Actor &actor, float dt, float time) {
    // 	actor.transform.setPosition(glm::vec3(2 * glm::sin(0.4 * time) - 2, 4.2f, 3.0f));
    // });
    // scene->addActor(std::make_shared<StaticMeshActor>(nanosuit2));

    // StaticMeshActor cube {RESOURCE_DIR "cube.obj"};
    // cube.transform.setScale(glm::vec3(0.5f));
    // cube.controller = new LambdaActorController([](Actor &actor, float dt, float time) {
    // 	actor.transform.setPosition(glm::vec3(2 * glm::sin(0.4 * time) + 2, 5.0f, 3.0f));
    // });
    // scene->addActor(std::make_shared<StaticMeshActor>(cube));

    Light mainlight;
    mainlight.type = Light::Type::Directional;
    mainlight.shadowCaster = true;
    mainlight.position = glm::vec3(12.0f, 40.0f, -7.0f);
    mainlight.direction = glm::vec3(-0.38f, -0.88f, 0.2f);
    scene->addLight(mainlight);

    Light test;
    test.type = Light::Type::Point;
    test.position = glm::vec3(0.0f, 10.0f, 0.0f);
    test.color = glm::vec3(1.0f, 0.0f, 1.0f);
    scene->addLight(test);

    // Camera setup
    camera.position = glm::vec3(5, 1, 0);
    camera.yaw = 180.0f;
    camera.update(0.0f);

    GLQuad::init();

    {
        glCreateBuffers(1, &voxelizeInfoSSBO);
        glNamedBufferStorage(voxelizeInfoSSBO, sizeof(VoxelizeInfo), nullptr, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, voxelizeInfoSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    glPatchParameteri(GL_PATCH_VERTICES, 3);
    // const GLfloat inner[] = { 2.f, 2.f };
    // const GLfloat outer[] = { 3.f, 3.f, 3.f, 3.f };
    // glPatchParameterfv(GL_PATCH_DEFAULT_INNER_LEVEL, inner);
    // glPatchParameterfv(GL_PATCH_DEFAULT_OUTER_LEVEL, outer);
}

void Application::update(float dt) {
    Mouse::update();

    if (GLFW_PRESS == Keyboard::getKey(GLFW_KEY_ESCAPE)) {
        glfwSetWindowShouldClose(window, 1);
    }

    if (Keyboard::getKeyTap(GLFW_KEY_LEFT_CONTROL)) {
        int mode = glfwGetInputMode(window, GLFW_CURSOR);
        glfwSetInputMode(window, GLFW_CURSOR, (mode == GLFW_CURSOR_DISABLED) ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    }

    if (Keyboard::getKeyTap(GLFW_KEY_GRAVE_ACCENT)) {
        ui.enabled = !ui.enabled;
    }

    if (Keyboard::getKeyTap(GLFW_KEY_V)) {
        settings.drawVoxels = !settings.drawVoxels;
    }

    if (Keyboard::getKeyTap(GLFW_KEY_T)) {
        settings.toggle = !settings.toggle;
    }

    if (GLFW_CURSOR_DISABLED == glfwGetInputMode(window, GLFW_CURSOR)) {
        camera.update(dt);
    }

    if (settings.voxelTrackCamera) {
        // To prevent temporal artifacts, the voxel textures are 'snapped' to a discrete grid
        glm::vec3 gridcell = glm::pow(2.f, (float)vct.voxelLevels) * (vct.max - vct.min) / (float)vct.voxelDim;
        vct.center = glm::floor(camera.position / gridcell) * gridcell;
    }

    scene->update(dt);
}

void Application::render(float dt) {
    totalTimer.start();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 projection = glm::perspective(camera.fov, (float)width / height, near, far);
    const glm::mat4 view = camera.lookAt();
    const glm::mat4 pv = glm::perspective(camera.fov, (float)width / height, 1.f, 20.f) * view;
    const glm::mat4 pvInverse = glm::inverse(pv);

    const Light mainlight = scene->lights[0];

    const float lz_near = 0.0f, lz_far = 100.0f, l_boundary = 25.0f;
    const glm::mat4 lp = glm::ortho(-l_boundary, l_boundary, -l_boundary, l_boundary, lz_near, lz_far);
    const glm::mat4 lv = glm::lookAt(mainlight.position, mainlight.position + mainlight.direction, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 ls = lp * lv;

    // Generate shadowmap
    shadowmapTimer.start();
    {
        GL_DEBUG_PUSH("Shadowmap")
        shadowmapFBO.bind();
        glViewport(0, 0, SHADOWMAP_WIDTH, SHADOWMAP_HEIGHT);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        shadowmapProgram.bind();
        shadowmapProgram.setUniformMatrix4fv("projection", lp);
        shadowmapProgram.setUniformMatrix4fv("view", lv);

        scene->draw(shadowmapProgram);

        shadowmapProgram.unbind();
        shadowmapFBO.unbind();

        GL_DEBUG_POP()
    }
    shadowmapTimer.stop();

    {
        // Create voxel occupancy grid
        GL_DEBUG_PUSH("Voxelize occupancy")

        glViewport(0, 0, vct.voxelOccupancyDim, vct.voxelOccupancyDim);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        switch (settings.conservativeRasterization) {
            case Settings::ConservativeRasterizeMode::NV:
                glEnable(GL_CONSERVATIVE_RASTERIZATION_NV);
                break;
            case Settings::ConservativeRasterizeMode::MSAA:
                glEnable(GL_MULTISAMPLE);
                break;
            case Settings::ConservativeRasterizeMode::OFF:
            default:
                // pass
                break;
        }

        glClearTexImage(vct.voxelOccupancy, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);

        glm::mat4 projection =
            glm::ortho(vct.min.x, vct.max.x, vct.min.y, vct.max.y, 0.0f, vct.max.z - vct.min.z);
        glm::mat4 mvp_x = projection * glm::lookAt(glm::vec3(vct.max.x, 0, 0) + vct.center,
                                                   vct.center, glm::vec3(0, 1, 0));
        glm::mat4 mvp_y = projection * glm::lookAt(glm::vec3(0, vct.max.y, 0) + vct.center,
                                                   vct.center, glm::vec3(0, 0, -1));
        glm::mat4 mvp_z = projection * glm::lookAt(glm::vec3(0, 0, vct.max.z) + vct.center,
                                                   vct.center, glm::vec3(0, 1, 0));

        voxelProgram.bind();
        voxelProgram.setUniformMatrix4fv("mvp_x", mvp_x);
        voxelProgram.setUniformMatrix4fv("mvp_y", mvp_y);
        voxelProgram.setUniformMatrix4fv("mvp_z", mvp_z);

        voxelProgram.setUniform1i("voxelizeOccupancy", GL_TRUE);

        glBindImageTexture(2, vct.voxelOccupancy, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

        scene->draw(voxelProgram);

        glBindImageTexture(2, 0, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
        voxelProgram.unbind();

        // Restore OpenGL state
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        switch (settings.conservativeRasterization) {
            case Settings::ConservativeRasterizeMode::NV:
                glDisable(GL_CONSERVATIVE_RASTERIZATION_NV);
                break;
            case Settings::ConservativeRasterizeMode::MSAA:
                glDisable(GL_MULTISAMPLE);
                break;
            case Settings::ConservativeRasterizeMode::OFF:
            default:
                // pass
                break;
        }

        GL_DEBUG_POP()

        // Create warp texture
        static GLuint warpTexture[warpDim][warpDim][warpDim];
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
        glGetTextureImage(
            vct.voxelOccupancy, 0,
            GL_RED_INTEGER, GL_UNSIGNED_INT,
            warpDim * warpDim * warpDim * sizeof(GLuint), warpTexture
        );

        // Compute partial sum tables
        static glm::ivec3 warpPartials[warpDim][warpDim][warpDim];

        for (int z = 0; z < warpDim; z++) {
            for (int y = 0; y < warpDim; y++) {
                int sumX = 0;
                for (int x = 0; x < warpDim; x++) {
                    sumX += warpTexture[z][y][x] > 0.5f ? 1 : 0;
                    warpPartials[z][y][x].x = sumX;
                }
            }
        }

        for (int z = 0; z < warpDim; z++) {
            for (int x = 0; x < warpDim; x++) {
                int sumY = 0;
                for (int y = 0; y < warpDim; y++) {
                    sumY += warpTexture[z][y][x] > 0.5f ? 1 : 0;
                    warpPartials[z][y][x].y = sumY;
                }
            }
        }

        for (int y = 0; y < warpDim; y++) {
            for (int x = 0; x < warpDim; x++) {
                int sumZ = 0;
                for (int z = 0; z < warpDim; z++) {
                    sumZ += warpTexture[z][y][x] > 0.5f ? 1 : 0;
                    warpPartials[z][y][x].z = sumZ;
                }
            }
        }

        // Calculate weights (index by # occupied cells, low and then high)
        static float warpWeights[2][warpDim + 1];
        for (int occupied = 0; occupied <= warpDim; occupied++) {
            if (occupied == 0 || occupied == warpDim) {
                // Linear scale if a row is all empty or all occupied
                warpWeights[0][occupied] = warpWeights[1][occupied] = 1.f;
            } else {
                int empty = warpDim - occupied; // number of empty cells along row
                int total = warpDim;            // total number of cells along row

                // Need to satisfy l * empty + h * occupied = total
                // Also set an upper and lower bound on resolution (this is a simple linear programming problem)

                // Set h to highest desired resolution
                float h = settings.warpTextureHighResolution;
                // Solve for l; if too low, solve for h instead
                float l = (total - h * occupied) / (float)empty;
                if (l < settings.warpTextureLowResolution) {
                    l = settings.warpTextureLowResolution;
                    h = (total - l * empty) / (float)occupied;
                }

                warpWeights[0][occupied] = l;
                warpWeights[1][occupied] = h;
            }
        }

        GL_DEBUG_PUSH("Generate Warpmap")

        // Use layered rendering to generate the warpmap
        static GLShaderProgram generateWarpmapShader{"Generate Warpmap",
                                                     {SHADER_DIR "quad.vert",
                                                      SHADER_DIR "generateWarpmap.geom",
                                                      SHADER_DIR "generateWarpmap.frag"}};
        static GLuint warpmapFBO = 0;
        static GLuint warpTextureId, warpPartialsId, warpWeightsId;
        if (warpmapFBO == 0) {
            // Create 3D warpmap
            glCreateTextures(GL_TEXTURE_3D, 1, &warpmap);
            glTextureParameteri(warpmap, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(warpmap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(warpmap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(warpmap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTextureParameteri(warpmap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTextureStorage3D(warpmap, 1, GL_RGBA16, warpDim, warpDim, warpDim);

            // Create layered framebuffer
            glCreateFramebuffers(1, &warpmapFBO);
            glNamedFramebufferTexture(warpmapFBO, GL_COLOR_ATTACHMENT0, warpmap, 0);
            glNamedFramebufferDrawBuffer(warpmapFBO, GL_COLOR_ATTACHMENT0);
            glNamedFramebufferReadBuffer(warpmapFBO, GL_NONE);
            if (glCheckNamedFramebufferStatus(warpmapFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                LOG_ERROR("Failed to create warpmapFBO");
            }

            // Create images to pass warpmap data
            glCreateTextures(GL_TEXTURE_3D, 1, &warpTextureId);
            glTextureStorage3D(warpTextureId, 1, GL_R32UI, warpDim, warpDim, warpDim);
            glClearTexImage(warpTextureId, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);

            glCreateTextures(GL_TEXTURE_3D, 1, &warpPartialsId);
            glTextureStorage3D(warpPartialsId, 1, GL_RGBA32I, warpDim, warpDim, warpDim);

            glCreateTextures(GL_TEXTURE_2D, 1, &warpWeightsId);
            glTextureStorage2D(warpWeightsId, 1, GL_R32F, warpDim + 1, 2);
        }

        // Upload warpmap data to GPU
        glTextureSubImage3D(
            warpTextureId, 0,
            0, 0, 0, warpDim, warpDim, warpDim,
            GL_RED_INTEGER, GL_UNSIGNED_INT, warpTexture
        );
        glTextureSubImage3D(
            warpPartialsId, 0,
            0, 0, 0, warpDim, warpDim, warpDim,
            GL_RGB_INTEGER, GL_INT, warpPartials
        );
        glTextureSubImage2D(
            warpWeightsId, 0,
            0, 0, warpDim + 1, 2,
            GL_RED, GL_FLOAT, warpWeights
        );

        // Compute warpmap weights separately
        static GLuint warpmapWeightsLow = 0, warpmapWeightsHigh = 0;
        if (settings.useWarpmapWeightsTexture) {
            static GLShaderProgram warpweightShader {"Generate Warp Weights", {SHADER_DIR "quad.vert",
                                                          SHADER_DIR "generateWarpmap.geom",
                                                          SHADER_DIR "generateWarpmapWeights.frag"}};
            static GLuint warpmapWeightsFBO = 0;
            if (warpmapWeightsFBO == 0) {
                // Create 3D warpmap
                glCreateTextures(GL_TEXTURE_3D, 1, &warpmapWeightsLow);
                glTextureParameteri(warpmapWeightsLow, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // TODO ?
                glTextureParameteri(warpmapWeightsLow, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // TODO ?
                glTextureParameteri(warpmapWeightsLow, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTextureParameteri(warpmapWeightsLow, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTextureParameteri(warpmapWeightsLow, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glTextureStorage3D(warpmapWeightsLow, 1, GL_RGBA16F, warpDim, warpDim, warpDim);

                glCreateTextures(GL_TEXTURE_3D, 1, &warpmapWeightsHigh);
                glTextureParameteri(warpmapWeightsHigh, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // TODO ?
                glTextureParameteri(warpmapWeightsHigh, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // TODO ?
                glTextureParameteri(warpmapWeightsHigh, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTextureParameteri(warpmapWeightsHigh, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTextureParameteri(warpmapWeightsHigh, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glTextureStorage3D(warpmapWeightsHigh, 1, GL_RGBA16F, warpDim, warpDim, warpDim);

                // Create layered framebuffer
                glCreateFramebuffers(1, &warpmapWeightsFBO);
                glNamedFramebufferTexture(warpmapWeightsFBO, GL_COLOR_ATTACHMENT0, warpmapWeightsLow, 0);
                glNamedFramebufferTexture(warpmapWeightsFBO, GL_COLOR_ATTACHMENT1, warpmapWeightsHigh, 0);
                GLuint attachments[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
                glNamedFramebufferDrawBuffers(warpmapWeightsFBO, 2, attachments);
                glNamedFramebufferReadBuffer(warpmapWeightsFBO, GL_NONE);
                if (glCheckNamedFramebufferStatus(warpmapWeightsFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    LOG_ERROR("Failed to create warpmapWeightsFBO");
                }
            }

            glBindFramebuffer(GL_FRAMEBUFFER, warpmapWeightsFBO);
            glViewport(0, 0, warpDim, warpDim);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            warpweightShader.bind();

            // glBindImageTexture(0, vct.voxelOccupancy, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
            glBindImageTexture(0, warpTextureId, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
            glBindImageTexture(1, warpPartialsId, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32I);
            glBindImageTexture(2, warpWeightsId, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);

            warpweightShader.setUniform1i("warpDim", warpDim);
            warpweightShader.setUniform1f("maxWeight", settings.warpTextureHighResolution);

            const size_t layersPerRender = 32;
            for (size_t layerOffset = 0; layerOffset < warpDim; layerOffset += layersPerRender) {
                warpweightShader.setUniform1i("layerOffset", layerOffset);
                GLQuad::draw();
            }

            glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
            glBindImageTexture(1, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32I);
            glBindImageTexture(2, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
            warpweightShader.unbind();
            glViewport(0, 0, width, height);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            // Apply Gaussian blur to the weights
            if (settings.blurWarpmapWeights) {
                GL_DEBUG_PUSH("Blur Warpmap Weights")
                static GLShaderProgram blurShader {"Blur Shader", {SHADER_DIR "gaussianBlur.comp"}};

                static GLuint blurTemp = 0;
                if (blurTemp == 0) {
                    // Create temporary texture to hold intermediate blur results
                    glCreateTextures(GL_TEXTURE_3D, 1, &blurTemp);
                    glTextureParameteri(blurTemp, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // TODO ?
                    glTextureParameteri(blurTemp, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // TODO ?
                    glTextureParameteri(blurTemp, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTextureParameteri(blurTemp, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTextureParameteri(blurTemp, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                    glTextureStorage3D(blurTemp, 1, GL_RGBA16F, warpDim, warpDim, warpDim);
                }

                GLuint num_groups = (warpDim + 8 - 1) / 8;
                GLuint src = warpmapWeightsHigh, dst = blurTemp;
                blurShader.bind();
                for (size_t axis = 0; axis < 3; ++axis) {
                    blurShader.setUniform1i("axis", axis);
                    glBindImageTexture(0, src, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
                    glBindImageTexture(1, dst, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

                    glDispatchCompute(num_groups, num_groups, num_groups);
                    std::swap(src, dst);

                    // TODO need to blur both? or rederive?

                    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
                }
                glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
                glBindImageTexture(1, 0, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
                blurShader.unbind();

                GL_DEBUG_POP()
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, warpmapFBO);
        glViewport(0, 0, warpDim, warpDim);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        generateWarpmapShader.bind();

        // glBindImageTexture(0, vct.voxelOccupancy, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
        glBindImageTexture(0, warpTextureId, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
        glBindImageTexture(1, warpPartialsId, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32I);
        glBindImageTexture(2, warpWeightsId, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);

        if (settings.useWarpmapWeightsTexture) {
            glBindTextureUnit(0, warpmapWeightsLow);
            glBindTextureUnit(1, warpmapWeightsHigh);
        }

        generateWarpmapShader.setUniform1i("toggle", settings.toggle);
        generateWarpmapShader.setUniform1i("warpTextureLinear", settings.warpTextureLinear);
        glUniform3iv(generateWarpmapShader.uniformLocation("warpTextureAxes"), 1, settings.warpTextureAxes);
        generateWarpmapShader.setUniform1i("warpDim", warpDim);
        generateWarpmapShader.setUniform1i("useWarpmapWeightsTexture", settings.useWarpmapWeightsTexture);
        generateWarpmapShader.setUniform1f("maxWeight", settings.warpTextureHighResolution);

        const size_t layersPerRender = 32;
        for (size_t layerOffset = 0; layerOffset < warpDim; layerOffset += layersPerRender) {
            generateWarpmapShader.setUniform1i("layerOffset", layerOffset);
        GLQuad::draw();
        }

        glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
        glBindImageTexture(1, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32I);
        glBindImageTexture(2, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
        if (settings.useWarpmapWeightsTexture) {
            glBindTextureUnit(0, 0);
            glBindTextureUnit(1, 0);
        }
        generateWarpmapShader.unbind();
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        GL_DEBUG_POP()
    }

    // TODO does this need to be synchronized before rendering?
    glClearNamedBufferData(voxelizeInfoSSBO, GL_R32UI, GL_RED, GL_UNSIGNED_INT, nullptr);

    voxelizeTimer.start();
    // Voxelize scene
    if (settings.voxelizeTesselation) {
        GL_DEBUG_PUSH("Voxelize Tesselation")

        static GLShaderProgram voxelizeTesselationShader {
            "Voxelize Tesselation",
            {
                SHADER_DIR "simpleTesselated.vert",
                SHADER_DIR "testTesselation.tesc",
                SHADER_DIR "testTesselation.tese"
            }};
        static GLShaderProgram voxelizeTesselationDebugShader{
            "Voxelize Tesselation Debug",
            {
                // SHADER_DIR "quad.vert",
                SHADER_DIR "simpleTesselated.vert",
                SHADER_DIR "testTesselation.tesc",
                SHADER_DIR "testTesselation.tese",
                SHADER_DIR "debugVoxelsTesselated.geom",
                SHADER_DIR "debugVoxels.frag"
                // SHADER_DIR "testTesselation.frag"
            }};

        GLShaderProgram *shader = &voxelizeTesselationShader;

        if (settings.voxelizeTesselationDebug) {

            glViewport(0, 0, width, height);
            glPolygonMode(GL_FRONT_AND_BACK, settings.drawWireframe ? GL_LINE : GL_FILL);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);

            shader = &voxelizeTesselationDebugShader;
        }
        else {

            glEnable(GL_RASTERIZER_DISCARD);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);

            shader = &voxelizeTesselationShader;
        }

        glClearTexImage(vct.voxelColor, 0, GL_RGBA, GL_FLOAT, nullptr);
        glClearTexImage(vct.voxelNormal, 0, GL_RGBA, GL_FLOAT, nullptr);

        shader->bind();

        shader->setUniformMatrix4fv("projection", projection);
        shader->setUniformMatrix4fv("view", view);
        shader->setUniform1i("voxelizeAtomicMax", settings.voxelizeAtomicMax);
        shader->setUniform1i("voxelizeTesselationWarp", settings.voxelizeTesselationWarp);
        shader->setUniform1f("voxelDim", vct.voxelDim);
        shader->setUniform3fv("voxelMin", vct.min);
        shader->setUniform3fv("voxelMax", vct.max);
        shader->setUniform3fv("voxelCenter", vct.center);
        shader->setUniformMatrix4fv("pv", pv);

        glBindImageTexture(0, vct.voxelColor, 0, GL_TRUE, 0, GL_READ_WRITE, vct.useRGBA16f ? GL_RGBA16F : GL_R32UI);
        glBindImageTexture(1, vct.voxelNormal, 0, GL_TRUE, 0, GL_READ_WRITE, vct.useRGBA16f ? GL_RGBA16F : GL_R32UI);

        // GLQuad::draw(GL_PATCHES);
        scene->draw(*shader, GL_PATCHES);

        glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
        glBindImageTexture(1, 0, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

        glDisable(GL_RASTERIZER_DISCARD);

        shader->unbind();
        GL_DEBUG_POP()

        // Render overlay
        if (settings.voxelizeTesselationDebug) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

            GL_DEBUG_PUSH("Render Overlay")
            ui.render(dt);
            GL_DEBUG_POP()
            return;
        }
    }
    else {
        GL_DEBUG_PUSH("Voxelize Scene")
        glViewport(0, 0, settings.voxelizeMultiplier * vct.voxelDim, settings.voxelizeMultiplier * vct.voxelDim);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        switch (settings.conservativeRasterization) {
            case Settings::ConservativeRasterizeMode::NV:
                glEnable(GL_CONSERVATIVE_RASTERIZATION_NV);
                break;
            case Settings::ConservativeRasterizeMode::MSAA:
                glEnable(GL_MULTISAMPLE);
                break;
            case Settings::ConservativeRasterizeMode::OFF:
            default:
                // pass
                break;
        }

        glClearTexImage(vct.voxelColor, 0, GL_RGBA, GL_FLOAT, nullptr);
        glClearTexImage(vct.voxelNormal, 0, GL_RGBA, GL_FLOAT, nullptr);

        glm::mat4 projection = glm::ortho(vct.min.x, vct.max.x, vct.min.y, vct.max.y, 0.0f, vct.max.z - vct.min.z);
        glm::mat4 mvp_x = projection * glm::lookAt(glm::vec3(vct.max.x, 0, 0) + vct.center, vct.center, glm::vec3(0, 1, 0));
        glm::mat4 mvp_y = projection * glm::lookAt(glm::vec3(0, vct.max.y, 0) + vct.center, vct.center, glm::vec3(0, 0, -1));
        glm::mat4 mvp_z = projection * glm::lookAt(glm::vec3(0, 0, vct.max.z) + vct.center, vct.center, glm::vec3(0, 1, 0));

        voxelProgram.bind();
        voxelProgram.setUniformMatrix4fv("mvp_x", mvp_x);
        voxelProgram.setUniformMatrix4fv("mvp_y", mvp_y);
        voxelProgram.setUniformMatrix4fv("mvp_z", mvp_z);

        voxelProgram.setUniform1i("voxelizeOccupancy", GL_FALSE);

        voxelProgram.setUniform1i("axis_override", settings.axisOverride);

        voxelProgram.setUniform3fv("eye", camera.position);
        voxelProgram.setUniform3fv("lightPos", mainlight.position);
        voxelProgram.setUniform3fv("lightInt", mainlight.color);
        voxelProgram.setUniform1i("voxelizeDilate", settings.voxelizeDilate);
        voxelProgram.setUniform1i("warpVoxels", settings.warpVoxels);
        voxelProgram.setUniform1i("warpTexture", settings.warpTexture);
        voxelProgram.setUniform1i("voxelizeAtomicMax", settings.voxelizeAtomicMax);
        voxelProgram.setUniform1i("toggle", settings.toggle);
        voxelProgram.setUniform1i("voxelizeLighting", settings.voxelizeLighting);
        voxelProgram.setUniform3fv("voxelMin", vct.min);
        voxelProgram.setUniform3fv("voxelMax", vct.max);
        voxelProgram.setUniform3fv("voxelCenter", vct.center);

        glBindImageTexture(0, vct.voxelColor, 0, GL_TRUE, 0, GL_READ_WRITE, vct.useRGBA16f ? GL_RGBA16F : GL_R32UI);
        glBindImageTexture(1, vct.voxelNormal, 0, GL_TRUE, 0, GL_READ_WRITE, vct.useRGBA16f ? GL_RGBA16F : GL_R32UI);

        scene->bindLightSSBO(3);
        voxelProgram.setUniformMatrix4fv("ls", ls);

        GLuint shadowmap = shadowmapFBO.getTexture(0);
        glBindTextureUnit(6, shadowmap);

        glBindTextureUnit(10, warpmap);

        scene->draw(voxelProgram);

        glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
        glBindImageTexture(1, 0, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
        glBindTextureUnit(6, 0);
        glBindTextureUnit(10, 0);
        voxelProgram.unbind();

        // Restore OpenGL state
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        switch (settings.conservativeRasterization) {
            case Settings::ConservativeRasterizeMode::NV:
                glDisable(GL_CONSERVATIVE_RASTERIZATION_NV);
                break;
            case Settings::ConservativeRasterizeMode::MSAA:
                glDisable(GL_MULTISAMPLE);
                break;
            case Settings::ConservativeRasterizeMode::OFF:
            default:
                // pass
                break;
        }
        GL_DEBUG_POP()
    }
    voxelizeTimer.stop();

    {
        GL_DEBUG_PUSH("Transfer Voxels")

        static GLShaderProgram transferVoxels { "Transfer Voxels", {SHADER_DIR "transferVoxels.comp"}};

        if (!settings.temporalFilterRadiance) {
            glClearTexImage(vct.voxelRadiance, 0, GL_RGBA, GL_FLOAT, nullptr);
        }

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        transferVoxels.bind();
        transferVoxels.setUniform1i("temporalFilter", settings.temporalFilterRadiance);
        transferVoxels.setUniform1f("temporalDecay", settings.temporalDecay);
        transferVoxels.setUniform1f("voxelSetOpacity", settings.voxelSetOpacity);

        glBindImageTexture(0, vct.voxelColor, 0, GL_TRUE, 0, GL_READ_WRITE, vct.voxelFormat);
        glBindImageTexture(1, vct.voxelNormal, 0, GL_TRUE, 0, GL_READ_WRITE, vct.voxelFormat);
        glBindImageTexture(2, vct.voxelRadiance, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);

        glDispatchCompute((vct.voxelDim + 8 - 1) / 8, (vct.voxelDim + 8 - 1) / 8, (vct.voxelDim + 8 - 1) / 8);

        glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_WRITE, vct.voxelFormat);
        glBindImageTexture(1, 0, 0, GL_TRUE, 0, GL_READ_WRITE, vct.voxelFormat);
        glBindImageTexture(2, 0, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
        transferVoxels.unbind();

        GL_DEBUG_POP()
    }

    // Inject radiance into voxel grid
    radianceTimer.start();
    {
        GL_DEBUG_PUSH("Radiance Injection")

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        injectRadianceProgram.bind();

        glBindImageTexture(0, vct.voxelColor, 0, GL_TRUE, 0, GL_READ_ONLY, vct.voxelFormat);
        glBindImageTexture(1, vct.voxelNormal, 0, GL_TRUE, 0, GL_READ_ONLY, vct.voxelFormat);
        glBindImageTexture(2, vct.voxelRadiance, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

        GLuint shadowmap = shadowmapFBO.getTexture(0);
        glBindTextureUnit(1, shadowmap);
        injectRadianceProgram.setUniform1i("shadowmap", 1);

        glm::mat4 lsInverse = glm::inverse(ls);
        injectRadianceProgram.setUniformMatrix4fv("lsInverse", lsInverse);
        injectRadianceProgram.setUniform3fv("lightPos", mainlight.position);
        injectRadianceProgram.setUniform3fv("lightInt", mainlight.color);

        injectRadianceProgram.setUniform3fv("eye", camera.position);
        injectRadianceProgram.setUniform1i("warpVoxels", settings.warpVoxels);
        injectRadianceProgram.setUniform1i("warpTexture", settings.warpTexture);
        injectRadianceProgram.setUniform1i("voxelizeTesselationWarp", settings.voxelizeTesselationWarp);
        injectRadianceProgram.setUniformMatrix4fv("pv", pv);
        injectRadianceProgram.setUniform1i("voxelDim", vct.voxelDim);
        injectRadianceProgram.setUniform3fv("voxelMin", vct.min);
        injectRadianceProgram.setUniform3fv("voxelMax", vct.max);
        injectRadianceProgram.setUniform3fv("voxelCenter", vct.center);

        injectRadianceProgram.setUniform1i("radianceLighting", settings.radianceLighting);
        injectRadianceProgram.setUniform1i("radianceDilate", settings.radianceDilate);
        injectRadianceProgram.setUniform1i("temporalFilterRadiance", settings.temporalFilterRadiance);
        injectRadianceProgram.setUniform1f("temporalDecay", settings.temporalDecay);

        glBindTextureUnit(10, warpmap);

        // 2D workgroup should be the size of shadowmap, local_size = 16
        glDispatchCompute((SHADOWMAP_WIDTH + 16 - 1) / 16, (SHADOWMAP_HEIGHT + 16 - 1) / 16, 1);

        glBindTextureUnit(1, 0);
        glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
        glBindImageTexture(1, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
        glBindImageTexture(2, 0, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
        injectRadianceProgram.unbind();

        GL_DEBUG_POP()
    }
    radianceTimer.stop();

    if (settings.voxelFillHoles) {
        GL_DEBUG_PUSH("Voxel Fill Holes")

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        static GLShaderProgram fillHoles {"Voxel Fill Holes", {SHADER_DIR "voxelFillHoles.comp"}};
        static GLuint filledVoxels = 0;
        if (filledVoxels == 0) {
                glCreateTextures(GL_TEXTURE_3D, 1, &filledVoxels);
                glTextureParameteri(filledVoxels, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // TODO ?
                glTextureParameteri(filledVoxels, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // TODO ?
                glTextureParameteri(filledVoxels, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTextureParameteri(filledVoxels, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTextureParameteri(filledVoxels, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glTextureStorage3D(filledVoxels, 1, GL_RGBA8, vct.voxelDim, vct.voxelDim, vct.voxelDim);
        }

        fillHoles.bind();
        glBindImageTexture(0, vct.voxelRadiance, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA8);
        glBindImageTexture(1, filledVoxels, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);

        GLuint num_groups = (vct.voxelDim + 8 - 1) / 8;
        glDispatchCompute(num_groups, num_groups, num_groups);

        glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA8);
        glBindImageTexture(1, 0, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
        fillHoles.unbind();

        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
        glCopyImageSubData(
            filledVoxels, GL_TEXTURE_3D, 0, 0, 0, 0,
            vct.voxelRadiance, GL_TEXTURE_3D, 0, 0, 0, 0,
            vct.voxelDim, vct.voxelDim, vct.voxelDim
        );

        GL_DEBUG_POP()
    }

    mipmapTimer.start();
    {
        // glGenerateTextureMipmap(vct.voxelColor);
        // glGenerateTextureMipmap(vct.voxelNormal);
        // glGenerateTextureMipmap(vct.voxelRadiance);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        mipmapProgram.bind();

        int dim = vct.voxelDim;
        const int local_size = 8;
        for (int level = 0; level < vct.voxelLevels; level++) {
            glBindImageTexture(0, vct.voxelRadiance, level, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA8);
            glBindImageTexture(1, vct.voxelRadiance, level + 1, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);

            GLuint num_groups = ((dim >> 1) + local_size - 1) / local_size;
            glDispatchCompute(num_groups, num_groups, num_groups);

            glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA8);
            glBindImageTexture(1, 0, 1, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);

            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

            dim >>= 1;
        }
        dim = vct.voxelDim;
        for (int level = 0; level < vct.voxelLevels; level++) {
            glBindImageTexture(0, vct.voxelColor, level, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA8);
            glBindImageTexture(1, vct.voxelColor, level + 1, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);

            GLuint num_groups = ((dim >> 1) + local_size - 1) / local_size;
            glDispatchCompute(num_groups, num_groups, num_groups);

            glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA8);
            glBindImageTexture(1, 0, 1, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);

            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

            dim >>= 1;
        }

        mipmapProgram.unbind();
    }
    mipmapTimer.stop();

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    if (settings.debugVoxels) {
        glm::mat4 mvp = projection * view;
        debugVoxels(settings.drawRadiance ? vct.voxelRadiance : vct.voxelColor, mvp);
    }
    else if (settings.raymarch) {
        viewRaymarched();
    }
    else if (settings.drawShadowmap) {
        view2DTexture(shadowmapFBO.getTexture(0));
    }
    else {
        // Depth prepass
        {
            GL_DEBUG_PUSH("Depth Prepass")

            glViewport(0, 0, width, height);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            if (settings.msaa) {
                glEnable(GL_MULTISAMPLE);
            }
            if (settings.alphatocoverage) {
                glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            }
            else {
                glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            }

            ditherProgram.bind();

            ditherProgram.setUniformMatrix4fv("projection", projection);
            ditherProgram.setUniformMatrix4fv("view", view);

            scene->draw(ditherProgram);

            ditherProgram.unbind();

            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            GL_DEBUG_POP()
        }

        renderTimer.start();
        // Render scene
        {
            GL_DEBUG_PUSH("Render Scene")

            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT);

            glViewport(0, 0, width, height);
            // glEnable(GL_FRAMEBUFFER_SRGB);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_EQUAL);
            glDepthMask(GL_FALSE);
            glEnable(GL_CULL_FACE);
            glPolygonMode(GL_FRONT_AND_BACK, settings.drawWireframe ? GL_LINE : GL_FILL);

            program.bind();
            program.setUniformMatrix4fv("projection", projection);
            program.setUniformMatrix4fv("view", view);
            program.setUniform3fv("eye", camera.position);
            program.setUniformMatrix4fv("ls", ls);

            GLuint shadowmap = shadowmapFBO.getTexture(0);
            glBindTextureUnit(6, shadowmap);

            program.setUniform1i("voxelize", settings.drawVoxels);
            program.setUniform1i("normals", settings.drawNormals);
            program.setUniform1i("dominant_axis", settings.drawDominantAxis);
            program.setUniform1i("radiance", settings.drawRadiance);
            program.setUniform1i("drawWarpSlope", settings.drawWarpSlope);
            program.setUniform1i("drawOcclusion", settings.drawOcclusion);
            program.setUniform1i("debugOcclusion", settings.debugOcclusion);
            program.setUniform1i("debugIndirect", settings.debugIndirect);
            program.setUniform1i("debugReflections", settings.debugReflections);
            program.setUniform1i("debugMaterialDiffuse", settings.debugMaterialDiffuse);
            program.setUniform1i("debugMaterialRoughness", settings.debugMaterialRoughness);
            program.setUniform1i("debugMaterialMetallic", settings.debugMaterialMetallic);
            program.setUniform1i("debugWarpTexture", settings.debugWarpTexture);
            program.setUniform1i("toggle", settings.toggle);

            program.setUniform1i("cooktorrance", settings.cooktorrance);
            program.setUniform1i("enablePostprocess", settings.enablePostprocess);
            program.setUniform1i("enableShadows", settings.enableShadows);
            program.setUniform1i("enableNormalMap", settings.enableNormalMap);
            program.setUniform1i("enableIndirect", settings.enableIndirect);
            program.setUniform1i("enableDiffuse", settings.enableDiffuse);
            program.setUniform1i("enableSpecular", settings.enableSpecular);
            program.setUniform1i("enableReflections", settings.enableReflections);
            program.setUniform1f("ambientScale", settings.ambientScale);
            program.setUniform1f("reflectScale", settings.reflectScale);

            program.setUniform1f("miplevel", settings.miplevel);

            program.setUniform1i("warpVoxels", settings.warpVoxels);
            program.setUniform1i("warpTexture", settings.warpTexture);
            program.setUniform1i("voxelizeTesselationWarp", settings.voxelizeTesselationWarp);
            program.setUniformMatrix4fv("pv", pv);
            program.setUniform1i("voxelDim", vct.voxelDim);
            program.setUniform3fv("voxelMin", vct.min);
            program.setUniform3fv("voxelMax", vct.max);
            program.setUniform3fv("voxelCenter", vct.center);
            glBindTextureUnit(2, vct.voxelColor);
            glBindTextureUnit(3, vct.voxelNormal);
            glBindTextureUnit(4, vct.voxelRadiance);

            program.setUniform1i("vctSteps", settings.diffuseConeSettings.steps);
            program.setUniform1f("vctBias", settings.diffuseConeSettings.bias);
            program.setUniform1f("vctConeAngle", settings.diffuseConeSettings.coneAngle);
            program.setUniform1f("vctConeInitialHeight", settings.diffuseConeSettings.coneInitialHeight);
            program.setUniform1f("vctLodOffset", settings.diffuseConeSettings.lodOffset);

            program.setUniform1i("vctSpecularSteps", settings.specularConeSettings.steps);
            program.setUniform1f("vctSpecularBias", settings.specularConeSettings.bias);
            program.setUniform1f("vctSpecularConeAngle", settings.specularConeSettings.coneAngle);
            program.setUniform1f("vctSpecularConeInitialHeight", settings.specularConeSettings.coneInitialHeight);
            program.setUniform1f("vctSpecularLodOffset", settings.specularConeSettings.lodOffset);
            program.setUniform1i("vctSpecularConeAngleFromRoughness", settings.specularConeAngleFromRoughness);

            glBindTextureUnit(10, warpmap);

            scene->bindLightSSBO(3);

            scene->draw(program);

            glBindTextureUnit(1, 0);
            glBindTextureUnit(2, 0);
            glBindTextureUnit(3, 0);
            glBindTextureUnit(4, 0);
            glBindTextureUnit(6, 0);
            glBindTextureUnit(10, 0);
            program.unbind();

            glDisable(GL_MULTISAMPLE);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            // glDisable(GL_FRAMEBUFFER_SRGB);
            if (settings.drawWireframe) {
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
            GL_DEBUG_POP()
        }
        renderTimer.stop();
    }

    totalTimer.stop();

    voxelizeTimer.getQueryResult();
    shadowmapTimer.getQueryResult();
    radianceTimer.getQueryResult();
    mipmapTimer.getQueryResult();
    renderTimer.getQueryResult();
    totalTimer.getQueryResult();

    // Render overlay
    {
        GL_DEBUG_PUSH("Render Overlay")
        ui.render(dt);
        GL_DEBUG_POP()
    }
}

// Create a 3D texture
GLuint make3DTexture(GLsizei size, GLsizei levels, GLenum internalFormat, GLint minFilter, GLint magFilter) {
    GLuint handle;

    glGenTextures(1, &handle);
    glBindTexture(GL_TEXTURE_3D, handle);

    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);

    glTexStorage3D(GL_TEXTURE_3D, levels, internalFormat, size, size, size);

    if (internalFormat == GL_R32UI) {
        glClearTexImage(handle, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
    }
    else {
        glClearTexImage(handle, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    }

    if (levels > 1) {
        glGenerateMipmap(GL_TEXTURE_3D);
    }

    glBindTexture(GL_TEXTURE_3D, 0);

    return handle;
}

// Dirty function that renders a texture to a full screen quad.
void view2DTexture(GLuint texture) {
    static const GLchar *vert =
        "#version 330\n"
        "layout(location = 0) in vec3 pos;\n"
        "layout(location = 1) in vec2 tc;\n"
        "out vec2 fragTexcoord;\n"
        "void main() {\n"
        "gl_Position = vec4(pos, 1);\n"
        "fragTexcoord = tc;\n"
        "}\n";

    static const GLchar *frag =
        "#version 420\n"
        "in vec2 fragTexcoord;\n"
        "out vec4 color;\n"
        "layout(binding = 0) uniform sampler2D texture0;\n"
        "void main() {\n"
        "color = vec4(texture(texture0, fragTexcoord).rgb, 1);\n"
        "}\n";

    static GLuint program = 0;
    if (program == 0) {
        GLuint shaders[2];
        shaders[0] = GLHelper::createShaderFromString(GL_VERTEX_SHADER, vert);
        shaders[1] = GLHelper::createShaderFromString(GL_FRAGMENT_SHADER, frag);
        program = glCreateProgram();
        glAttachShader(program, shaders[0]);
        glAttachShader(program, shaders[1]);
        glLinkProgram(program);
        if (!GLHelper::checkShaderProgramStatus(program)) {
            LOG_ERROR("Quad debug shader compilation failed");
        }
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);

    glBindTextureUnit(0, texture);
    GLQuad::draw();

    glUseProgram(0);
}

void Application::viewRaymarched() {
    static const GLchar *vert =
        "#version 330\n"
        "layout(location = 0) in vec3 pos;\n"
        "layout(location = 1) in vec2 tc;\n"
        "out vec2 fragTexcoord;\n"
        "void main() {\n"
        "gl_Position = vec4(pos, 1);\n"
        "fragTexcoord = tc;\n"
        "}\n";

    static GLuint program = 0;
    if (program == 0) {
        GLuint shaders[2];
        shaders[0] = GLHelper::createShaderFromString(GL_VERTEX_SHADER, vert);
        shaders[1] = GLHelper::createShaderFromFile(GL_FRAGMENT_SHADER, SHADER_DIR "raymarch.frag");
        program = glCreateProgram();
        glAttachShader(program, shaders[0]);
        glAttachShader(program, shaders[1]);
        glLinkProgram(program);
        if (!GLHelper::checkShaderProgramStatus(program)) {
            LOG_ERROR("Raymarch debug shader compilation failed");
        }
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);

    glBindTextureUnit(0, vct.voxelColor);
    glBindTextureUnit(1, vct.voxelNormal);
    glBindTextureUnit(2, vct.voxelRadiance);
    glBindTextureUnit(10, warpmap);

    glm::vec3 cameraRight = glm::normalize(glm::cross(camera.front, camera.up)) * ((float)width / height);

    glUniform3fv(glGetUniformLocation(program, "eye"), 1, glm::value_ptr(camera.position));
    glUniform3fv(glGetUniformLocation(program, "viewForward"), 1, glm::value_ptr(camera.front));
    glUniform3fv(glGetUniformLocation(program, "viewRight"), 1, glm::value_ptr(cameraRight));
    glUniform3fv(glGetUniformLocation(program, "viewUp"), 1, glm::value_ptr(glm::normalize(glm::cross(cameraRight, camera.front))));
    glUniform1i(glGetUniformLocation(program, "width"), width);
    glUniform1i(glGetUniformLocation(program, "height"), height);
    glUniform1f(glGetUniformLocation(program, "near"), near);
    glUniform1f(glGetUniformLocation(program, "far"), far);
    glUniform1i(glGetUniformLocation(program, "voxelDim"), vct.voxelDim);
    glUniform1i(glGetUniformLocation(program, "warpVoxels"), settings.warpVoxels);
    glUniform1i(glGetUniformLocation(program, "warpTexture"), settings.warpTexture);
    glUniform3fv(glGetUniformLocation(program, "voxelMin"), 1, glm::value_ptr(vct.min));
    glUniform3fv(glGetUniformLocation(program, "voxelMax"), 1, glm::value_ptr(vct.max));
    glUniform3fv(glGetUniformLocation(program, "voxelCenter"), 1, glm::value_ptr(vct.center));
    glUniform1f(glGetUniformLocation(program, "lod"), settings.miplevel);
    glUniform1i(glGetUniformLocation(program, "radiance"), settings.drawRadiance);

    GLQuad::draw();

    glBindTextureUnit(0, 0);
    glBindTextureUnit(1, 0);
    glBindTextureUnit(2, 0);
    glBindTextureUnit(10, 0);

    glUseProgram(0);
}

void Application::debugVoxels(GLuint texture_id, const glm::mat4 &mvp) {
    static const float point[] = { 0.0f, 0.0f, 0.0f };
    static GLuint vao = 0;
    static GLShaderProgram *shader = nullptr;
    if (!shader) {
        GLuint vbo;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(point), point, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (GLvoid *)0);
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        shader = new GLShaderProgram();
        shader->attachAndLink({SHADER_DIR "debugVoxels.vert", SHADER_DIR "debugVoxels.geom", SHADER_DIR "debugVoxels.frag"});
        shader->setObjectLabel("Debug Voxels");
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    if (settings.debugVoxelOpacity) {
        glEnable(GL_BLEND);
    }

    shader->bind();

    shader->setUniformMatrix4fv("mvp", mvp);
    shader->setUniform1f("level", settings.miplevel);
    shader->setUniform1f("voxelDim", vct.voxelDim);
    shader->setUniform3fv("voxelMin", vct.min);
    shader->setUniform3fv("voxelMax", vct.max);
    shader->setUniform3fv("voxelCenter", vct.center);
    shader->setUniform1i("debugOpacity", settings.debugVoxelOpacity);

    glBindTextureUnit(0, texture_id);

    glBindVertexArray(vao);
    glDrawArraysInstanced(GL_POINTS, 0, 1, vct.voxelDim * vct.voxelDim * vct.voxelDim);
    glBindVertexArray(0);

    glBindTextureUnit(0, 0);

    shader->unbind();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    if (settings.debugVoxelOpacity) {
        glDisable(GL_BLEND);
    }
}