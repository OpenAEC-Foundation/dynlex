#version 300 es

precision highp float;
precision highp int;

struct type_8 {
    float member;
};
vec4 gen_gl_Position = vec4(0.0, 0.0, 0.0, 1.0);

vec4 dynlex_vertex_506f736974696f6e_0_4_4_4_1 = vec4(0.0);

layout(std140) uniform type_8_block_0Vertex { type_8 _group_0_binding_0_vs; };

layout(std140) uniform type_8_block_1Vertex { type_8 _group_0_binding_2_vs; };

layout(std140) uniform type_8_block_2Vertex { type_8 _group_0_binding_3_vs; };

layout(std140) uniform type_8_block_3Vertex { type_8 _group_0_binding_1_vs; };

layout(location = 0) in vec4 _p2vs_location0;

void main_1() {
    uint phi_139_ = 0u;
    float phi_141_ = 0.0;
    uint phi_146_ = 0u;
    float phi_148_ = 0.0;
    uint phi_152_ = 0u;
    float phi_154_ = 0.0;
    uint phi_171_ = 0u;
    float phi_173_ = 0.0;
    vec4 _e70 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
    vec4 _e72 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
    vec4 _e74 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
    vec4 _e76 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
    float _e78 = floor((_e70.x * 0.00024414063));
    float _e80 = floor((_e72.y * 0.00024414063));
    float _e82 = floor((_e74.z * 0.00024414063));
    float _e85 = (((_e78 / 4095.0) * 4.0) + -2.0);
    float _e88 = (((_e80 / 4095.0) * 4.0) + -2.0);
    float _e91 = (((_e82 / 4095.0) * 4.0) + -2.0);
    float _e93 = _group_0_binding_0_vs.member;
    float _e95 = _group_0_binding_2_vs.member;
    float _e97 = _group_0_binding_3_vs.member;
    float _e99 = _group_0_binding_1_vs.member;
    float _e100 = min(_e93, 10.4);
    float _e101 = (_e100 / 0.55);
    if ((_e101 < 0.0)) {
        phi_141_ = 0.0;
    } else {
        phi_139_ = 0u;
        if ((_e101 > 1.0)) {
        } else {
            phi_139_ = 1u;
        }
        uint _e105 = phi_139_;
        phi_141_ = _e101;
        if ((0u == _e105)) {
            phi_141_ = 1.0;
        }
    }
    float _e108 = phi_141_;
    float _e110 = ((_e100 + -10.45) / 0.5500002);
    if ((_e110 < 0.0)) {
        phi_148_ = 0.0;
    } else {
        phi_146_ = 0u;
        if ((_e110 > 1.0)) {
        } else {
            phi_146_ = 1u;
        }
        uint _e114 = phi_146_;
        phi_148_ = _e110;
        if ((0u == _e114)) {
            phi_148_ = 1.0;
        }
    }
    float _e117 = phi_148_;
    float _e118 = (_e100 / 4.25);
    if ((_e118 < 0.0)) {
        phi_154_ = 0.0;
    } else {
        phi_152_ = 0u;
        if ((_e118 > 1.0)) {
        } else {
            phi_152_ = 1u;
        }
        uint _e122 = phi_152_;
        phi_154_ = _e118;
        if ((0u == _e122)) {
            phi_154_ = 1.0;
        }
    }
    float _e125 = phi_154_;
    float _e134 = (((sin((((_e85 * 3.13) + (_e88 * 2.71)) + (_e91 * 4.19))) * 0.5) + 0.5) * 0.75);
    float _e135 = (_e134 + 4.45);
    float _e139 = ((_e100 - _e135) / ((_e134 + 6.65) - _e135));
    if ((_e139 < 0.0)) {
        phi_173_ = 0.0;
    } else {
        phi_171_ = 0u;
        if ((_e139 > 1.0)) {
        } else {
            phi_171_ = 1u;
        }
        uint _e143 = phi_171_;
        phi_173_ = _e139;
        if ((0u == _e143)) {
            phi_173_ = 1.0;
        }
    }
    float _e146 = phi_173_;
    float _e154 = ((sin((((_e88 * 3.91) - (_e85 * 2.17)) + (_e91 * 2.43))) * 0.5) + 0.5);
    float _e159 = (sin((_e93 * 0.31)) * 0.34);
    float _e160 = cos(_e159);
    float _e162 = sin(_e159);
    float _e166 = ((((_e91 * _e160) - (_e85 * _e162)) * 0.52) + 3.25);
    float _e175 = (1.0 / (max(_e95, 1.0) / max(_e97, 1.0)));
    float _e180 = floor((_e76.w * 0.25));
    float _e186 = ((((_e70.x - (_e78 * 4096.0)) / 4095.0) * 4.0) + -2.0);
    vec3 _e190 = vec3(_e186, vec3(0.0).y, vec3(0.0).z);
    float _e195 = ((((_e72.y - (_e80 * 4096.0)) / 4095.0) * 4.0) + -2.0);
    vec3 _e199 = vec3(_e190.x, _e195, _e190.z);
    vec3 _e208 = vec3(_e199.x, _e199.y, ((((_e74.z - (_e82 * 4096.0)) / 4095.0) * 4.0) + -2.0));
    float _e210 = ((_e186 > 0.0) ? 0.72 : -0.72);
    float _e211 = (_e186 - _e210);
    float _e212 = (_e93 * -9.4);
    float _e213 = cos(_e212);
    float _e215 = (_e195 + 0.42);
    float _e216 = sin(_e212);
    vec3 _e223 = vec3((_e210 + ((_e213 * _e211) - (_e216 * _e215))), _e208.y, _e208.z);
    vec3 _e232 = ((_e180 > 0.5) ? vec3(_e223.x, (((_e213 * _e215) + (_e216 * _e211)) + -0.42), _e223.z) : _e208);
    float _e237 = ((_e125 * _e125) * (3.0 - (_e125 * 2.0)));
    float _e244 = ((_e237 * 0.04) + 1.3);
    float _e245 = cos(_e244);
    float _e248 = sin(_e244);
    float _e255 = max((((2.8 - (_e237 * 4.9)) + ((_e232.z * _e245) - (_e232.x * _e248))) + 4.2), 0.2);
    float _e256 = (((_e232.y + ((_e237 * 0.12) + -0.19)) * 1.72) / _e255);
    float _e265 = ((_e175 * ((((_e237 * 1.18) + -1.26) + ((_e232.x * _e245) + (_e232.z * _e248))) * 1.72)) / _e255);
    float _e277 = (_e76.w - (_e180 * 4.0));
    float _e281 = ((_e146 * _e146) * (3.0 - (_e146 * 2.0)));
    float _e283 = sin((_e281 * 3.1415927));
    float _e286 = (((_e154 * 0.24) + 0.1) * _e283);
    float _e289 = ((_e154 * 6.2831855) + (_e281 * 3.2));
    float _e308 = (_e255 + ((_e166 - _e255) * _e281));
    float _e311 = (((_e99 * 0.04) + 0.96) * 0.00104);
    float _e312 = (_e175 * _e311);
    vec2 _e316 = vec2(-(_e312), vec2(0.0).y);
    vec2 _e320 = vec2(_e316.x, -(_e311));
    vec2 _e331 = ((_e277 >= 2.0) ? vec2(vec2(0.0, 0.0).x, (_e311 * 1.45)) : ((_e277 >= 1.0) ? vec2(_e312, _e320.y) : _e320));
    vec2 _e337 = vec2((_e308 * (_e331.x + ((_e265 + ((((_e175 * (((_e85 * _e160) + (_e91 * _e162)) * 1.66)) / _e166) - _e265) * _e281)) + (_e175 * ((_e283 * 0.12) + (cos(_e289) * _e286)))))), vec2(0.0).y);
    vec2 _e343 = vec2(_e337.x, (_e308 * (_e331.y + ((_e256 + (((((_e88 * 1.86) + -0.04) / _e166) - _e256) * _e281)) + ((sin(_e289) * _e286) * 0.72)))));
    vec2 _e349 = (((((_e108 * _e108) * (3.0 - (_e108 * 2.0))) * (1.0 - ((_e117 * _e117) * (3.0 - (_e117 * 2.0))))) < 0.001) ? vec2((_e308 * 3.0), _e343.y) : _e343);
    vec4 _e355 = vec4(_e349.x, vec4(0.0).y, vec4(0.0).z, vec4(0.0).w);
    vec4 _e362 = vec4(_e355.x, -(_e349.y), _e355.z, _e355.w);
    vec4 _e368 = vec4(_e362.x, _e362.y, (_e308 * 0.42), _e362.w);
    gen_gl_Position = vec4(_e368.x, _e368.y, _e368.z, _e308);
    return;
}

void main() {
    vec4 dynlex_vertex_506f736974696f6e_0_4_4_4_ = _p2vs_location0;
    dynlex_vertex_506f736974696f6e_0_4_4_4_1 = dynlex_vertex_506f736974696f6e_0_4_4_4_;
    main_1();
    float _e4 = gen_gl_Position.y;
    gen_gl_Position.y = -(_e4);
    vec4 _e6 = gen_gl_Position;
    gl_Position = _e6;
    gl_Position.yz = vec2(-gl_Position.y, gl_Position.z * 2.0 - gl_Position.w);
    return;
}
