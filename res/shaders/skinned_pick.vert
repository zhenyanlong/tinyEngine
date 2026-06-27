#version 450

layout(push_constant) uniform Push {
    mat4 model;
    uint objectId;
} push;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 materialTint;
    vec4 boxMaterialTint;
    vec4 emissive;
    vec4 cameraPos;
    vec4 lightDir;
    vec4 lightColor;
    vec4 pbrFactors;
    mat4 viewProj;
    mat4 invView;
    mat4 invProj;
} ubo;

layout(binding = 6) uniform BoneMatricesBlock {
    mat4 bones[256];
} boneUBO;

layout(location = 0) in vec3 inPosition;
layout(location = 6) in ivec4 boneIndices;
layout(location = 7) in vec4 boneWeights;

void main() {
    mat4 skinMatrix = boneWeights.x * boneUBO.bones[boneIndices.x]
                    + boneWeights.y * boneUBO.bones[boneIndices.y]
                    + boneWeights.z * boneUBO.bones[boneIndices.z]
                    + boneWeights.w * boneUBO.bones[boneIndices.w];
    gl_Position = ubo.proj * ubo.view * push.model * skinMatrix * vec4(inPosition, 1.0);
}
