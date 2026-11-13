#version 300 es

precision highp float;
precision highp int;

struct type_8 {
    vec3 member;
};
struct type_10 {
    vec2 member;
};
struct type_15 {
    float member;
};
struct VertexOutput {
    vec4 member;
    vec4 member_1;
    vec4 member_2;
    vec4 member_3;
};
vec4 gen_gl_Position = vec4(0.0, 0.0, 0.0, 1.0);

layout(std140) uniform type_15_block_0Vertex { type_15 _group_0_binding_0_vs; };

layout(std140) uniform type_15_block_1Vertex { type_15 _group_0_binding_2_vs; };

layout(std140) uniform type_15_block_2Vertex { type_15 _group_0_binding_3_vs; };

vec4 dynlex_vertex_506f736974696f6e_0_4_4_4_1 = vec4(0.0);

vec4 dynlex_interpolant_7465727261696e20706f736974696f6e = vec4(0.0);

vec4 dynlex_interpolant_7465727261696e206e6f726d616c = vec4(0.0);

vec4 dynlex_interpolant_7465727261696e206d6174657269616c = vec4(0.0);

layout(location = 0) in vec4 _p2vs_location0;
smooth out vec4 _vs2fs_location2;
smooth out vec4 _vs2fs_location1;
smooth out vec4 _vs2fs_location0;

float the_simplex_corner_at_3a_point8offset5_with_permutation_3a_value8permutation5_definition_a79b700c9a74741d6a8594c4143b02a273848b138fdb550ea0ee8b10da594efa_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(inout type_10 offset, inout float permutation) {
    vec2 _e129 = offset.member;
    float _e136 = max((0.5 - ((_e129.x * _e129.x) + (_e129.y * _e129.y))), 0.0);
    float _e137 = permutation;
    float _e138 = (_e137 / 41.0);
    float _e142 = (((_e138 - floor(_e138)) * 2.0) + -1.0);
    float _e144 = (abs(_e142) + -0.5);
    float _e147 = (_e142 - floor((_e142 + 0.5)));
    float _e153 = (_e136 * _e136);
    return (((_e129.y * _e144) + (_e129.x * _e147)) * ((_e153 * _e153) * (1.7928429 - (((_e144 * _e144) + (_e147 * _e147)) * 0.85373473))));
}

float the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(inout type_10 point, inout float phase) {
    float final = 0.0;
    float following = 0.0;
    float initial = 0.0;
    type_10 third = type_10(vec2(0.0));
    type_10 middle = type_10(vec2(0.0));
    type_10 offset_1 = type_10(vec2(0.0));
    vec2 _e135 = point.member;
    float _e137 = phase;
    float _e139 = (_e135.x + (_e137 * 17.17));
    float _e142 = (_e135.y - (_e137 * 11.13));
    float _e144 = ((_e139 + _e142) * 0.3660254);
    float _e146 = floor((_e139 + _e144));
    float _e148 = floor((_e142 + _e144));
    float _e150 = ((_e146 + _e148) * 0.21132487);
    float _e152 = (_e139 - (_e146 - _e150));
    vec2 _e155 = vec2(_e152, vec2(0.0).y);
    float _e157 = (_e142 - (_e148 - _e150));
    offset_1.member = vec2(_e155.x, _e157);
    vec2 _e163 = ((_e152 > _e157) ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
    vec2 _e169 = vec2(((_e152 - _e163.x) + 0.21132487), vec2(0.0).y);
    middle.member = vec2(_e169.x, ((_e157 - _e163.y) + 0.21132487));
    vec2 _e180 = vec2((_e152 + -0.57735026), vec2(0.0).y);
    third.member = vec2(_e180.x, (_e157 + -0.57735026));
    float _e189 = (_e146 - (floor((_e146 / 289.0)) * 289.0));
    float _e193 = (_e148 - (floor((_e148 / 289.0)) * 289.0));
    float _e196 = (_e193 * ((_e193 * 34.0) + 1.0));
    float _e201 = (_e189 + (_e196 - (floor((_e196 / 289.0)) * 289.0)));
    float _e204 = (_e201 * ((_e201 * 34.0) + 1.0));
    initial = (_e204 - (floor((_e204 / 289.0)) * 289.0));
    float _e209 = (_e193 + _e163.y);
    float _e212 = (_e209 * ((_e209 * 34.0) + 1.0));
    float _e218 = (_e163.x + (_e189 + (_e212 - (floor((_e212 / 289.0)) * 289.0))));
    float _e221 = (_e218 * ((_e218 * 34.0) + 1.0));
    following = (_e221 - (floor((_e221 / 289.0)) * 289.0));
    float _e226 = (_e193 + 1.0);
    float _e229 = (_e226 * ((_e226 * 34.0) + 1.0));
    float _e235 = ((_e189 + (_e229 - (floor((_e229 / 289.0)) * 289.0))) + 1.0);
    float _e238 = (_e235 * ((_e235 * 34.0) + 1.0));
    final = (_e238 - (floor((_e238 / 289.0)) * 289.0));
    float _e243 = the_simplex_corner_at_3a_point8offset5_with_permutation_3a_value8permutation5_definition_a79b700c9a74741d6a8594c4143b02a273848b138fdb550ea0ee8b10da594efa_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(offset_1, initial);
    float _e244 = the_simplex_corner_at_3a_point8offset5_with_permutation_3a_value8permutation5_definition_a79b700c9a74741d6a8594c4143b02a273848b138fdb550ea0ee8b10da594efa_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(middle, following);
    float _e245 = the_simplex_corner_at_3a_point8offset5_with_permutation_3a_value8permutation5_definition_a79b700c9a74741d6a8594c4143b02a273848b138fdb550ea0ee8b10da594efa_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(third, final);
    return (((_e243 + _e244) + _e245) * 130.0);
}

float the_terrain_height_at_3terrain_coordinate8position5_definition_639ad5d0194d1bc7dd52debb7607c0b010b85e5153abf4f756c57e66871a1ab7_a_20_point_type_b0089e5f41b1d10807c0e489b7fcbaac18702385abdec1ea99534a1b4507d459_(inout type_8 position) {
    float tmp178_ = 0.0;
    type_10 tmp177_ = type_10(vec2(0.0));
    float tmp148_ = 0.0;
    type_10 tmp147_ = type_10(vec2(0.0));
    float tmp117_ = 0.0;
    type_10 tmp116_ = type_10(vec2(0.0));
    float tmp60_ = 0.0;
    type_10 tmp59_ = type_10(vec2(0.0));
    float tmp28_ = 0.0;
    type_10 tmp27_ = type_10(vec2(0.0));
    float tmp9_ = 0.0;
    type_10 tmp8_ = type_10(vec2(0.0));
    uint phi_473_ = 0u;
    float phi_475_ = 0.0;
    uint phi_483_ = 0u;
    float phi_485_ = 0.0;
    vec3 _e140 = position.member;
    vec2 _e145 = vec2((_e140.x * 0.011), vec2(0.0).y);
    tmp8_.member = vec2(_e145.x, (_e140.z * 0.011));
    tmp9_ = 1.7;
    float _e152 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp8_, tmp9_);
    vec2 _e157 = vec2(((_e140.x * 0.021) + 9.0), vec2(0.0).y);
    tmp27_.member = vec2(_e157.x, ((_e140.z * 0.021) + -4.0));
    tmp28_ = 4.1;
    float _e164 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp27_, tmp28_);
    vec2 _e170 = vec2(((_e140.z * 0.019) - (_e140.x * 0.006)), vec2(0.0).y);
    tmp59_.member = vec2(_e170.x, ((_e140.z * 0.007) + (_e140.x * 0.02)));
    tmp60_ = 8.3;
    float _e178 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp59_, tmp60_);
    float _e180 = (_e140.x + (_e164 * 9.5));
    float _e182 = (_e140.z + (_e178 * 9.5));
    vec2 _e188 = vec2(((_e180 * 0.027) + (_e182 * 0.006)), vec2(0.0).y);
    tmp116_.member = vec2(_e188.x, ((_e182 * 0.027) - (_e180 * 0.006)));
    tmp117_ = 2.3;
    float _e196 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp116_, tmp117_);
    vec2 _e202 = vec2(((_e182 * 0.057) - (_e180 * 0.018)), vec2(0.0).y);
    tmp147_.member = vec2(_e202.x, ((_e180 * 0.054) + (_e182 * 0.019)));
    tmp148_ = 6.9;
    float _e210 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp147_, tmp148_);
    vec2 _e216 = vec2(((_e180 * 0.119) + (_e182 * 0.041)), vec2(0.0).y);
    tmp177_.member = vec2(_e216.x, ((_e182 * 0.113) - (_e180 * 0.037)));
    tmp178_ = 9.7;
    float _e224 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp177_, tmp178_);
    float _e226 = ((_e152 + 0.48) / 0.92999995);
    if ((_e226 < 0.0)) {
        phi_475_ = 0.0;
    } else {
        phi_473_ = 0u;
        if ((_e226 > 1.0)) {
        } else {
            phi_473_ = 1u;
        }
        uint _e230 = phi_473_;
        phi_475_ = _e226;
        if ((0u == _e230)) {
            phi_475_ = 1.0;
        }
    }
    float _e233 = phi_475_;
    float _e238 = ((abs((_e210 + (_e224 * 0.24))) + -0.035) / 0.275);
    if ((_e238 < 0.0)) {
        phi_485_ = 0.0;
    } else {
        phi_483_ = 0u;
        if ((_e238 > 1.0)) {
        } else {
            phi_483_ = 1u;
        }
        uint _e242 = phi_483_;
        phi_485_ = _e238;
        if ((0u == _e242)) {
            phi_485_ = 1.0;
        }
    }
    float _e245 = phi_485_;
    float _e249 = ((_e233 * _e233) * (3.0 - (_e233 * 2.0)));
    float _e256 = max(((1.0 - abs(((_e196 * 0.78) + (_e210 * 0.34)))) + -0.42), 0.0);
    float _e262 = (((_e196 * 0.6) + (_e210 * 0.27)) + (_e224 * 0.13));
    float _e263 = max(_e262, 0.0);
    float _e264 = (_e263 * _e263);
    return (((_e152 * 1.2) + ((((_e224 * _e249) * 0.34) + ((((_e264 * 2.4) + 3.1) * ((_e256 * _e256) * _e249)) + ((_e262 * 1.38) + (_e264 * ((_e249 * 6.0) + 2.0))))) - (((_e264 * 0.38) + 0.16) * (_e249 * (1.0 - ((_e245 * _e245) * (3.0 - (_e245 * 2.0)))))))) + -0.75);
}

void main_1() {
    float tmp416_ = 0.0;
    type_10 tmp415_ = type_10(vec2(0.0));
    float tmp395_ = 0.0;
    type_10 tmp394_ = type_10(vec2(0.0));
    type_8 tmp357_ = type_8(vec3(0.0));
    type_8 tmp344_ = type_8(vec3(0.0));
    type_8 tmp331_ = type_8(vec3(0.0));
    type_8 tmp318_ = type_8(vec3(0.0));
    type_8 world = type_8(vec3(0.0));
    uint phi_304_ = 0u;
    float phi_306_ = 0.0;
    float phi_340_ = 0.0;
    float phi_341_ = 0.0;
    float phi_342_ = 0.0;
    vec3 phi_343_ = vec3(0.0);
    float phi_344_ = 0.0;
    vec4 phi_392_ = vec4(0.0);
    float _e136 = _group_0_binding_0_vs.member;
    float _e138 = _group_0_binding_2_vs.member;
    float _e141 = _group_0_binding_3_vs.member;
    float _e143 = (max(_e138, 1.0) / max(_e141, 1.0));
    vec4 _e144 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
    vec4 _e147 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
    if ((_e144.w < 0.5)) {
        vec4 _e448 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
        vec4 _e453 = vec4(_e147.x, _e147.x, vec4(0.0).z, vec4(0.0).w);
        dynlex_interpolant_7465727261696e20706f736974696f6e = vec4(_e453.x, _e448.y, _e453.z, _e453.w);
        dynlex_interpolant_7465727261696e206e6f726d616c = vec4(0.0, 1.0, 0.0, 0.0);
        dynlex_interpolant_7465727261696e206d6174657269616c = vec4(0.5, 0.0, 0.0, 0.0);
        vec4 _e459 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
        vec4 _e460 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
        vec4 _e465 = vec4(_e459.x, _e459.x, vec4(0.0, 0.0, 0.9999, 1.0).z, vec4(0.0, 0.0, 0.9999, 1.0).w);
        phi_392_ = vec4(_e465.x, _e460.y, _e465.z, _e465.w);
    } else {
        float _e165 = ((sin((_e136 * 0.073)) * 0.13) + (cos((_e136 * 0.041)) * 0.045));
        float _e166 = sin(_e165);
        float _e167 = cos(_e165);
        vec4 _e168 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
        float _e171 = ((_e143 * _e168.x) * 0.8);
        float _e172 = (_e171 * 0.98801154);
        float _e176 = (_e147.y / sqrt(((_e172 * _e172) + 1.0)));
        float _e177 = (_e176 * 0.98801154);
        float _e179 = (_e171 * (_e177 + 1.9760653));
        float _e183 = ((_e166 * _e176) + ((((sin((_e136 * 0.091)) * 4.2) + (cos((_e136 * 0.037)) * 1.6)) + 201.7) + (_e167 * _e179)));
        vec3 _e187 = vec3(_e183, vec3(0.0, 0.0, 0.0).y, vec3(0.0, 0.0, 0.0).z);
        float _e191 = ((((_e136 * 3.25) + -92.1) + (_e167 * _e176)) - (_e166 * _e179));
        world.member = vec3(_e187.x, _e187.y, _e191);
        float _e198 = the_terrain_height_at_3terrain_coordinate8position5_definition_639ad5d0194d1bc7dd52debb7607c0b010b85e5153abf4f756c57e66871a1ab7_a_20_point_type_b0089e5f41b1d10807c0e489b7fcbaac18702385abdec1ea99534a1b4507d459_(world);
        if ((_e144.w > 1.5)) {
            float _e273 = ((_e136 * 0.41) + ((_e191 * 0.07) + (_e183 * 0.17)));
            float _e278 = (((_e191 * 0.21) - (_e183 * 0.05)) - (_e136 * 0.32));
            float _e283 = ((_e136 * 0.23) + ((_e191 * 0.13) + (_e183 * 0.09)));
            float _e284 = sin(_e273);
            float _e285 = sin(_e278);
            float _e286 = sin(_e283);
            float _e287 = cos(_e273);
            float _e288 = cos(_e278);
            float _e289 = cos(_e283);
            float _e291 = ((_e147.y + -48.0) / 48.0);
            if ((_e291 < 0.0)) {
                phi_306_ = 0.0;
            } else {
                phi_304_ = 0u;
                if ((_e291 > 1.0)) {
                } else {
                    phi_304_ = 1u;
                }
                uint _e295 = phi_304_;
                phi_306_ = _e291;
                if ((0u == _e295)) {
                    phi_306_ = 1.0;
                }
            }
            float _e298 = phi_306_;
            float _e303 = (1.0 - ((_e298 * _e298) * (3.0 - (_e298 * 2.0))));
            vec3 _e321 = vec3((_e303 * -(((_e289 * 0.00162) + ((_e287 * 0.00765) - (_e288 * 0.0014))))), vec3(0.0, 1.0, 0.0).y, vec3(0.0, 1.0, 0.0).z);
            phi_340_ = max((-0.62 - _e198), 0.0);
            phi_341_ = ((_e286 * 0.5) * _e303);
            phi_342_ = (((_e284 + _e285) * 0.25) * _e303);
            phi_343_ = vec3(_e321.x, _e321.y, (_e303 * -(((_e289 * 0.00234) + ((_e287 * 0.00315) + (_e288 * 0.00588))))));
            phi_344_ = ((((_e286 * 0.018) + ((_e284 * 0.045) + (_e285 * 0.028))) * _e303) + -0.62);
        } else {
            vec3 _e203 = vec3((_e183 + -0.34), vec3(0.0, 0.0, 0.0).y, vec3(0.0, 0.0, 0.0).z);
            tmp318_.member = vec3(_e203.x, _e203.y, _e191);
            float _e209 = the_terrain_height_at_3terrain_coordinate8position5_definition_639ad5d0194d1bc7dd52debb7607c0b010b85e5153abf4f756c57e66871a1ab7_a_20_point_type_b0089e5f41b1d10807c0e489b7fcbaac18702385abdec1ea99534a1b4507d459_(tmp318_);
            vec3 _e214 = vec3((_e183 + 0.34), vec3(0.0, 0.0, 0.0).y, vec3(0.0, 0.0, 0.0).z);
            tmp331_.member = vec3(_e214.x, _e214.y, _e191);
            float _e220 = the_terrain_height_at_3terrain_coordinate8position5_definition_639ad5d0194d1bc7dd52debb7607c0b010b85e5153abf4f756c57e66871a1ab7_a_20_point_type_b0089e5f41b1d10807c0e489b7fcbaac18702385abdec1ea99534a1b4507d459_(tmp331_);
            tmp344_.member = vec3(_e187.x, _e187.y, (_e191 + -0.34));
            float _e227 = the_terrain_height_at_3terrain_coordinate8position5_definition_639ad5d0194d1bc7dd52debb7607c0b010b85e5153abf4f756c57e66871a1ab7_a_20_point_type_b0089e5f41b1d10807c0e489b7fcbaac18702385abdec1ea99534a1b4507d459_(tmp344_);
            tmp357_.member = vec3(_e187.x, _e187.y, (_e191 + 0.34));
            float _e234 = the_terrain_height_at_3terrain_coordinate8position5_definition_639ad5d0194d1bc7dd52debb7607c0b010b85e5153abf4f756c57e66871a1ab7_a_20_point_type_b0089e5f41b1d10807c0e489b7fcbaac18702385abdec1ea99534a1b4507d459_(tmp357_);
            vec3 _e239 = vec3((_e209 - _e220), vec3(0.0, 0.68, 0.0).y, vec3(0.0, 0.68, 0.0).z);
            vec2 _e248 = vec2((_e183 * 0.065), vec2(0.0).y);
            tmp394_.member = vec2(_e248.x, (_e191 * 0.065));
            tmp395_ = 13.7;
            float _e254 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp394_, tmp395_);
            vec2 _e259 = vec2((_e183 * 0.14), vec2(0.0).y);
            tmp415_.member = vec2(_e259.x, (_e191 * 0.14));
            tmp416_ = 16.2;
            float _e265 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp415_, tmp416_);
            phi_340_ = 0.0;
            phi_341_ = (_e265 * 0.5);
            phi_342_ = (_e254 * 0.5);
            phi_343_ = vec3(_e239.x, _e239.y, (_e227 - _e234));
            phi_344_ = _e198;
        }
        float _e339 = phi_340_;
        float _e341 = phi_341_;
        float _e343 = phi_342_;
        vec3 _e345 = phi_343_;
        float _e347 = phi_344_;
        float _e358 = sqrt(((_e345.z * _e345.z) + ((_e345.x * _e345.x) + (_e345.y * _e345.y))));
        float _e362 = (_e347 + -12.8);
        float _e367 = (_e177 - (_e362 * 0.1543801));
        vec4 _e376 = vec4(_e183, vec4(0.0).y, vec4(0.0).z, vec4(0.0).w);
        vec4 _e381 = vec4(_e376.x, _e347, _e376.z, _e376.w);
        vec4 _e386 = vec4(_e381.x, _e381.y, _e191, _e381.w);
        dynlex_interpolant_7465727261696e20706f736974696f6e = vec4(_e386.x, _e386.y, _e386.z, _e144.w);
        vec4 _e396 = vec4((_e345.x / _e358), vec4(0.0).y, vec4(0.0).z, vec4(0.0).w);
        vec4 _e401 = vec4(_e396.x, (_e345.y / _e358), _e396.z, _e396.w);
        vec4 _e406 = vec4(_e401.x, _e401.y, (_e345.z / _e358), _e401.w);
        dynlex_interpolant_7465727261696e206e6f726d616c = vec4(_e406.x, _e406.y, _e406.z, _e147.y);
        vec4 _e416 = vec4((_e343 + 0.5), vec4(0.0).y, vec4(0.0).z, vec4(0.0).w);
        vec4 _e421 = vec4(_e416.x, (_e341 + 0.5), _e416.z, _e416.w);
        dynlex_interpolant_7465727261696e206d6174657269616c = vec4(_e421.x, _e421.y, _e339, _e421.w);
        vec4 _e431 = vec4((_e179 / (_e143 * 0.72)), vec4(0.0).y, vec4(0.0).z, vec4(0.0).w);
        vec4 _e437 = vec4(_e431.x, (((_e176 * 0.1543801) + (_e362 * 0.98801154)) / -0.72), _e431.z, _e431.w);
        vec4 _e442 = vec4(_e437.x, _e437.y, ((_e367 * 1.00078) + -0.40016), _e437.w);
        phi_392_ = vec4(_e442.x, _e442.y, _e442.z, _e367);
    }
    vec4 _e472 = phi_392_;
    gen_gl_Position = _e472;
    return;
}

void main() {
    vec4 dynlex_vertex_506f736974696f6e_0_4_4_4_ = _p2vs_location0;
    dynlex_vertex_506f736974696f6e_0_4_4_4_1 = dynlex_vertex_506f736974696f6e_0_4_4_4_;
    main_1();
    float _e7 = gen_gl_Position.y;
    gen_gl_Position.y = -(_e7);
    vec4 _e9 = gen_gl_Position;
    vec4 _e10 = dynlex_interpolant_7465727261696e20706f736974696f6e;
    vec4 _e11 = dynlex_interpolant_7465727261696e206e6f726d616c;
    vec4 _e12 = dynlex_interpolant_7465727261696e206d6174657269616c;
    VertexOutput _tmp_return = VertexOutput(_e9, _e10, _e11, _e12);
    gl_Position = _tmp_return.member;
    _vs2fs_location2 = _tmp_return.member_1;
    _vs2fs_location1 = _tmp_return.member_2;
    _vs2fs_location0 = _tmp_return.member_3;
    gl_Position.yz = vec2(-gl_Position.y, gl_Position.z * 2.0 - gl_Position.w);
    return;
}
