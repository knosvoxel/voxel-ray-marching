#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include "compute.h"

#include <vector>

#include "ogt_wrapper.h"

using namespace glm;

typedef struct RotationData {
	glm::vec4 instance_size;
	glm::vec4 rotated_size;
	glm::vec4 min_bounds;
	glm::mat4 transform;
};

typedef struct VoxelModel {
	glm::vec3 position_offset;
	uint32_t bit_offset;
	glm::ivec3 size;
	uint32_t padding;
};

class VoxScene {
public:
	VoxScene() {};
	~VoxScene() {};

	void VoxScene::load(const char* path, ComputeShader& cam_compute);

	std::vector<uint8_t> voxel_data;
	std::vector<VoxelModel> model_data;

	uint32_t model_array_size;

private:
	ogt_vox_model apply_rotations(const ogt_vox_scene* scene, RotationData& rotation_data, uint32_t instance_idx, ComputeShader& compute);

	ComputeShader apply_rotations_compute;

	// buffers
	GLuint voxel_data_buffer, model_data_buffer, model_size_buffer, palette, instance_temp_ssbo, rotated_temp_ssbo, rotation_data_temp_buffer;

};