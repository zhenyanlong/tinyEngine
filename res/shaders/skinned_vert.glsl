#version 450
#extension GL_ARB_separate_shader_objects : enable

const int kMaxBones = 256;

// Push constants: model matrix + pre-computed normal matrix (128 bytes total).
layout(push_constant) uniform PushModel {
    mat4 model;        // offset   0
    mat4 normalMatrix; // offset  64
} pushModel;

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
    mat4 bones[kMaxBones];
} boneUBO;

// Vertex attributes (location=3 reserved for per-instance data used by box pipeline)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in vec4 inTangent;
layout(location = 6) in ivec4 inBoneIndices;
layout(location = 7) in vec4  inBoneWeights;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out vec3 vWorldNormal;
layout(location = 4) out vec4 vWorldTangent;

void main() {
    mat4 skinMatrix =
        inBoneWeights.x * boneUBO.bones[inBoneIndices.x] +
        inBoneWeights.y * boneUBO.bones[inBoneIndices.y] +
        inBoneWeights.z * boneUBO.bones[inBoneIndices.z] +
        inBoneWeights.w * boneUBO.bones[inBoneIndices.w];

    vec4 skinnedPos = skinMatrix * vec4(inPosition, 1.0);
    vec4 wp         = pushModel.model * skinnedPos;
    vWorldPos       = wp.xyz;
    gl_Position     = ubo.viewProj * wp;

    vColor = inColor;
    vUV    = inTexCoord;

    mat3 nMat       = mat3(pushModel.normalMatrix);
    mat3 skinMat3   = mat3(skinMatrix);
    vWorldNormal    = nMat * skinMat3 * inNormal;
    vWorldTangent   = vec4(nMat * skinMat3 * inTangent.xyz, inTangent.w);
}
