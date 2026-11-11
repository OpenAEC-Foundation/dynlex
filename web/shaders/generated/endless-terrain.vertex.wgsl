struct type_8 {
    member: vec3<f32>,
}

struct type_10 {
    member: vec2<f32>,
}

struct type_15 {
    member: f32,
}

struct VertexOutput {
    @builtin(position) member: vec4<f32>,
    @location(2) member_1: vec4<f32>,
    @location(1) member_2: vec4<f32>,
    @location(0) member_3: vec4<f32>,
}

var<private> gl_Position: vec4<f32> = vec4<f32>(0f, 0f, 0f, 1f);
@group(0) @binding(0) 
var<uniform> ubo_time: type_15;
@group(0) @binding(2) 
var<uniform> ubo_width: type_15;
@group(0) @binding(3) 
var<uniform> ubo_height: type_15;
var<private> dynlex_vertex_506f736974696f6e_0_4_4_4_1: vec4<f32>;
var<private> dynlex_interpolant_7465727261696e20706f736974696f6e: vec4<f32>;
var<private> dynlex_interpolant_7465727261696e206e6f726d616c: vec4<f32>;
var<private> dynlex_interpolant_7465727261696e206d6174657269616c: vec4<f32>;

fn the_simplex_corner_at_3a_point8offset5_with_permutation_3a_value8permutation5_definition_a79b700c9a74741d6a8594c4143b02a273848b138fdb550ea0ee8b10da594efa_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(offset: ptr<function, type_10>, permutation: ptr<function, f32>) -> f32 {
    let _e129 = (*offset).member;
    let _e136 = max((0.5f - ((_e129.x * _e129.x) + (_e129.y * _e129.y))), 0f);
    let _e137 = (*permutation);
    let _e138 = (_e137 / 41f);
    let _e142 = (((_e138 - floor(_e138)) * 2f) + -1f);
    let _e144 = (abs(_e142) + -0.5f);
    let _e147 = (_e142 - floor((_e142 + 0.5f)));
    let _e153 = (_e136 * _e136);
    return (((_e129.y * _e144) + (_e129.x * _e147)) * ((_e153 * _e153) * (1.7928429f - (((_e144 * _e144) + (_e147 * _e147)) * 0.85373473f))));
}

fn the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(point: ptr<function, type_10>, phase: ptr<function, f32>) -> f32 {
    var final_: f32;
    var following: f32;
    var initial: f32;
    var third: type_10;
    var middle: type_10;
    var offset_1: type_10;

    let _e135 = (*point).member;
    let _e137 = (*phase);
    let _e139 = (_e135.x + (_e137 * 17.17f));
    let _e142 = (_e135.y - (_e137 * 11.13f));
    let _e144 = ((_e139 + _e142) * 0.3660254f);
    let _e146 = floor((_e139 + _e144));
    let _e148 = floor((_e142 + _e144));
    let _e150 = ((_e146 + _e148) * 0.21132487f);
    let _e152 = (_e139 - (_e146 - _e150));
    let _e155 = vec2<f32>(_e152, vec2<f32>().y);
    let _e157 = (_e142 - (_e148 - _e150));
    offset_1.member = vec2<f32>(_e155.x, _e157);
    let _e163 = select(vec2<f32>(0f, 1f), vec2<f32>(1f, 0f), (_e152 > _e157));
    let _e169 = vec2<f32>(((_e152 - _e163.x) + 0.21132487f), vec2<f32>().y);
    middle.member = vec2<f32>(_e169.x, ((_e157 - _e163.y) + 0.21132487f));
    let _e180 = vec2<f32>((_e152 + -0.57735026f), vec2<f32>().y);
    third.member = vec2<f32>(_e180.x, (_e157 + -0.57735026f));
    let _e189 = (_e146 - (floor((_e146 / 289f)) * 289f));
    let _e193 = (_e148 - (floor((_e148 / 289f)) * 289f));
    let _e196 = (_e193 * ((_e193 * 34f) + 1f));
    let _e201 = (_e189 + (_e196 - (floor((_e196 / 289f)) * 289f)));
    let _e204 = (_e201 * ((_e201 * 34f) + 1f));
    initial = (_e204 - (floor((_e204 / 289f)) * 289f));
    let _e209 = (_e193 + _e163.y);
    let _e212 = (_e209 * ((_e209 * 34f) + 1f));
    let _e218 = (_e163.x + (_e189 + (_e212 - (floor((_e212 / 289f)) * 289f))));
    let _e221 = (_e218 * ((_e218 * 34f) + 1f));
    following = (_e221 - (floor((_e221 / 289f)) * 289f));
    let _e226 = (_e193 + 1f);
    let _e229 = (_e226 * ((_e226 * 34f) + 1f));
    let _e235 = ((_e189 + (_e229 - (floor((_e229 / 289f)) * 289f))) + 1f);
    let _e238 = (_e235 * ((_e235 * 34f) + 1f));
    final_ = (_e238 - (floor((_e238 / 289f)) * 289f));
    let _e243 = the_simplex_corner_at_3a_point8offset5_with_permutation_3a_value8permutation5_definition_a79b700c9a74741d6a8594c4143b02a273848b138fdb550ea0ee8b10da594efa_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&offset_1), (&initial));
    let _e244 = the_simplex_corner_at_3a_point8offset5_with_permutation_3a_value8permutation5_definition_a79b700c9a74741d6a8594c4143b02a273848b138fdb550ea0ee8b10da594efa_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&middle), (&following));
    let _e245 = the_simplex_corner_at_3a_point8offset5_with_permutation_3a_value8permutation5_definition_a79b700c9a74741d6a8594c4143b02a273848b138fdb550ea0ee8b10da594efa_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&third), (&final_));
    return (((_e243 + _e244) + _e245) * 130f);
}

fn the_terrain_height_at_3terrain_coordinate8position5_definition_639ad5d0194d1bc7dd52debb7607c0b010b85e5153abf4f756c57e66871a1ab7_a_20_point_type_b0089e5f41b1d10807c0e489b7fcbaac18702385abdec1ea99534a1b4507d459_(position: ptr<function, type_8>) -> f32 {
    var tmp178_: f32;
    var tmp177_: type_10;
    var tmp148_: f32;
    var tmp147_: type_10;
    var tmp117_: f32;
    var tmp116_: type_10;
    var tmp60_: f32;
    var tmp59_: type_10;
    var tmp28_: f32;
    var tmp27_: type_10;
    var tmp9_: f32;
    var tmp8_: type_10;
    var phi_473_: u32;
    var phi_475_: f32;
    var phi_483_: u32;
    var phi_485_: f32;

    let _e140 = (*position).member;
    let _e145 = vec2<f32>((_e140.x * 0.011f), vec2<f32>().y);
    tmp8_.member = vec2<f32>(_e145.x, (_e140.z * 0.011f));
    tmp9_ = 1.7f;
    let _e152 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp8_), (&tmp9_));
    let _e157 = vec2<f32>(((_e140.x * 0.021f) + 9f), vec2<f32>().y);
    tmp27_.member = vec2<f32>(_e157.x, ((_e140.z * 0.021f) + -4f));
    tmp28_ = 4.1f;
    let _e164 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp27_), (&tmp28_));
    let _e170 = vec2<f32>(((_e140.z * 0.019f) - (_e140.x * 0.006f)), vec2<f32>().y);
    tmp59_.member = vec2<f32>(_e170.x, ((_e140.z * 0.007f) + (_e140.x * 0.02f)));
    tmp60_ = 8.3f;
    let _e178 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp59_), (&tmp60_));
    let _e180 = (_e140.x + (_e164 * 9.5f));
    let _e182 = (_e140.z + (_e178 * 9.5f));
    let _e188 = vec2<f32>(((_e180 * 0.027f) + (_e182 * 0.006f)), vec2<f32>().y);
    tmp116_.member = vec2<f32>(_e188.x, ((_e182 * 0.027f) - (_e180 * 0.006f)));
    tmp117_ = 2.3f;
    let _e196 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp116_), (&tmp117_));
    let _e202 = vec2<f32>(((_e182 * 0.057f) - (_e180 * 0.018f)), vec2<f32>().y);
    tmp147_.member = vec2<f32>(_e202.x, ((_e180 * 0.054f) + (_e182 * 0.019f)));
    tmp148_ = 6.9f;
    let _e210 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp147_), (&tmp148_));
    let _e216 = vec2<f32>(((_e180 * 0.119f) + (_e182 * 0.041f)), vec2<f32>().y);
    tmp177_.member = vec2<f32>(_e216.x, ((_e182 * 0.113f) - (_e180 * 0.037f)));
    tmp178_ = 9.7f;
    let _e224 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp177_), (&tmp178_));
    let _e226 = ((_e152 + 0.48f) / 0.92999995f);
    if (_e226 < 0f) {
        phi_475_ = 0f;
    } else {
        phi_473_ = 0u;
        if (_e226 > 1f) {
        } else {
            phi_473_ = 1u;
        }
        let _e230 = phi_473_;
        phi_475_ = _e226;
        if (0u == _e230) {
            phi_475_ = 1f;
        }
    }
    let _e233 = phi_475_;
    let _e238 = ((abs((_e210 + (_e224 * 0.24f))) + -0.035f) / 0.275f);
    if (_e238 < 0f) {
        phi_485_ = 0f;
    } else {
        phi_483_ = 0u;
        if (_e238 > 1f) {
        } else {
            phi_483_ = 1u;
        }
        let _e242 = phi_483_;
        phi_485_ = _e238;
        if (0u == _e242) {
            phi_485_ = 1f;
        }
    }
    let _e245 = phi_485_;
    let _e249 = ((_e233 * _e233) * (3f - (_e233 * 2f)));
    let _e256 = max(((1f - abs(((_e196 * 0.78f) + (_e210 * 0.34f)))) + -0.42f), 0f);
    let _e262 = (((_e196 * 0.6f) + (_e210 * 0.27f)) + (_e224 * 0.13f));
    let _e263 = max(_e262, 0f);
    let _e264 = (_e263 * _e263);
    return (((_e152 * 1.2f) + ((((_e224 * _e249) * 0.34f) + ((((_e264 * 2.4f) + 3.1f) * ((_e256 * _e256) * _e249)) + ((_e262 * 1.38f) + (_e264 * ((_e249 * 6f) + 2f))))) - (((_e264 * 0.38f) + 0.16f) * (_e249 * (1f - ((_e245 * _e245) * (3f - (_e245 * 2f)))))))) + -0.75f);
}

fn main_1() {
    var tmp410_: f32;
    var tmp409_: type_10;
    var tmp389_: f32;
    var tmp388_: type_10;
    var tmp351_: type_8;
    var tmp338_: type_8;
    var tmp325_: type_8;
    var tmp312_: type_8;
    var world: type_8;
    var phi_304_: u32;
    var phi_306_: f32;
    var phi_340_: f32;
    var phi_341_: f32;
    var phi_342_: f32;
    var phi_343_: vec3<f32>;
    var phi_344_: f32;
    var phi_392_: vec4<f32>;

    let _e136 = ubo_time.member;
    let _e138 = ubo_width.member;
    let _e141 = ubo_height.member;
    let _e143 = (max(_e138, 1f) / max(_e141, 1f));
    let _e144 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
    let _e147 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
    if (_e144.w < 0.5f) {
        let _e448 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
        let _e453 = vec4<f32>(_e147.x, _e147.x, vec4<f32>().z, vec4<f32>().w);
        dynlex_interpolant_7465727261696e20706f736974696f6e = vec4<f32>(_e453.x, _e448.y, _e453.z, _e453.w);
        dynlex_interpolant_7465727261696e206e6f726d616c = vec4<f32>(0f, 1f, 0f, 0f);
        dynlex_interpolant_7465727261696e206d6174657269616c = vec4<f32>(0.5f, 0f, 0f, 0f);
        let _e459 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
        let _e460 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
        let _e465 = vec4<f32>(_e459.x, _e459.x, vec4<f32>(f32(), f32(), 0.9999f, 1f).z, vec4<f32>(f32(), f32(), 0.9999f, 1f).w);
        phi_392_ = vec4<f32>(_e465.x, _e460.y, _e465.z, _e465.w);
    } else {
        let _e165 = ((sin((_e136 * 0.073f)) * 0.13f) + (cos((_e136 * 0.041f)) * 0.045f));
        let _e166 = sin(_e165);
        let _e167 = cos(_e165);
        let _e168 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
        let _e171 = ((_e143 * _e168.x) * 0.8f);
        let _e172 = (_e171 * 0.98801154f);
        let _e176 = (_e147.y / sqrt(((_e172 * _e172) + 1f)));
        let _e177 = (_e176 * 0.98801154f);
        let _e179 = (_e171 * (_e177 + 1.9760653f));
        let _e183 = ((_e166 * _e176) + ((((sin((_e136 * 0.091f)) * 4.2f) + (cos((_e136 * 0.037f)) * 1.6f)) + 201.7f) + (_e167 * _e179)));
        let _e187 = vec3<f32>(_e183, vec3<f32>(f32(), 0f, f32()).y, vec3<f32>(f32(), 0f, f32()).z);
        let _e191 = ((((_e136 * 3.25f) + -92.1f) + (_e167 * _e176)) - (_e166 * _e179));
        world.member = vec3<f32>(_e187.x, _e187.y, _e191);
        let _e198 = the_terrain_height_at_3terrain_coordinate8position5_definition_639ad5d0194d1bc7dd52debb7607c0b010b85e5153abf4f756c57e66871a1ab7_a_20_point_type_b0089e5f41b1d10807c0e489b7fcbaac18702385abdec1ea99534a1b4507d459_((&world));
        if (_e144.w > 1.5f) {
            let _e273 = ((_e136 * 0.41f) + ((_e191 * 0.07f) + (_e183 * 0.17f)));
            let _e278 = (((_e191 * 0.21f) - (_e183 * 0.05f)) - (_e136 * 0.32f));
            let _e283 = ((_e136 * 0.23f) + ((_e191 * 0.13f) + (_e183 * 0.09f)));
            let _e284 = sin(_e273);
            let _e285 = sin(_e278);
            let _e286 = sin(_e283);
            let _e287 = cos(_e273);
            let _e288 = cos(_e278);
            let _e289 = cos(_e283);
            let _e291 = ((_e147.y + -48f) / 48f);
            if (_e291 < 0f) {
                phi_306_ = 0f;
            } else {
                phi_304_ = 0u;
                if (_e291 > 1f) {
                } else {
                    phi_304_ = 1u;
                }
                let _e295 = phi_304_;
                phi_306_ = _e291;
                if (0u == _e295) {
                    phi_306_ = 1f;
                }
            }
            let _e298 = phi_306_;
            let _e303 = (1f - ((_e298 * _e298) * (3f - (_e298 * 2f))));
            let _e321 = vec3<f32>((_e303 * -(((_e289 * 0.00162f) + ((_e287 * 0.00765f) - (_e288 * 0.0014f))))), vec3<f32>(f32(), 1f, f32()).y, vec3<f32>(f32(), 1f, f32()).z);
            phi_340_ = max((-0.62f - _e198), 0f);
            phi_341_ = ((_e286 * 0.5f) * _e303);
            phi_342_ = (((_e284 + _e285) * 0.25f) * _e303);
            phi_343_ = vec3<f32>(_e321.x, _e321.y, (_e303 * -(((_e289 * 0.00234f) + ((_e287 * 0.00315f) + (_e288 * 0.00588f))))));
            phi_344_ = ((((_e286 * 0.018f) + ((_e284 * 0.045f) + (_e285 * 0.028f))) * _e303) + -0.62f);
        } else {
            let _e203 = vec3<f32>((_e183 + -0.34f), vec3<f32>(f32(), 0f, f32()).y, vec3<f32>(f32(), 0f, f32()).z);
            tmp312_.member = vec3<f32>(_e203.x, _e203.y, _e191);
            let _e209 = the_terrain_height_at_3terrain_coordinate8position5_definition_639ad5d0194d1bc7dd52debb7607c0b010b85e5153abf4f756c57e66871a1ab7_a_20_point_type_b0089e5f41b1d10807c0e489b7fcbaac18702385abdec1ea99534a1b4507d459_((&tmp312_));
            let _e214 = vec3<f32>((_e183 + 0.34f), vec3<f32>(f32(), 0f, f32()).y, vec3<f32>(f32(), 0f, f32()).z);
            tmp325_.member = vec3<f32>(_e214.x, _e214.y, _e191);
            let _e220 = the_terrain_height_at_3terrain_coordinate8position5_definition_639ad5d0194d1bc7dd52debb7607c0b010b85e5153abf4f756c57e66871a1ab7_a_20_point_type_b0089e5f41b1d10807c0e489b7fcbaac18702385abdec1ea99534a1b4507d459_((&tmp325_));
            tmp338_.member = vec3<f32>(_e187.x, _e187.y, (_e191 + -0.34f));
            let _e227 = the_terrain_height_at_3terrain_coordinate8position5_definition_639ad5d0194d1bc7dd52debb7607c0b010b85e5153abf4f756c57e66871a1ab7_a_20_point_type_b0089e5f41b1d10807c0e489b7fcbaac18702385abdec1ea99534a1b4507d459_((&tmp338_));
            tmp351_.member = vec3<f32>(_e187.x, _e187.y, (_e191 + 0.34f));
            let _e234 = the_terrain_height_at_3terrain_coordinate8position5_definition_639ad5d0194d1bc7dd52debb7607c0b010b85e5153abf4f756c57e66871a1ab7_a_20_point_type_b0089e5f41b1d10807c0e489b7fcbaac18702385abdec1ea99534a1b4507d459_((&tmp351_));
            let _e239 = vec3<f32>((_e209 - _e220), vec3<f32>(f32(), 0.68f, f32()).y, vec3<f32>(f32(), 0.68f, f32()).z);
            let _e248 = vec2<f32>((_e183 * 0.065f), vec2<f32>().y);
            tmp388_.member = vec2<f32>(_e248.x, (_e191 * 0.065f));
            tmp389_ = 13.7f;
            let _e254 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp388_), (&tmp389_));
            let _e259 = vec2<f32>((_e183 * 0.14f), vec2<f32>().y);
            tmp409_.member = vec2<f32>(_e259.x, (_e191 * 0.14f));
            tmp410_ = 16.2f;
            let _e265 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_definition_9ec9c146ebc7a85119d4bcf0b82ba2832980bf28047655187412e8f0c4cb8803_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp409_), (&tmp410_));
            phi_340_ = 0f;
            phi_341_ = (_e265 * 0.5f);
            phi_342_ = (_e254 * 0.5f);
            phi_343_ = vec3<f32>(_e239.x, _e239.y, (_e227 - _e234));
            phi_344_ = _e198;
        }
        let _e339 = phi_340_;
        let _e341 = phi_341_;
        let _e343 = phi_342_;
        let _e345 = phi_343_;
        let _e347 = phi_344_;
        let _e358 = sqrt(((_e345.z * _e345.z) + ((_e345.x * _e345.x) + (_e345.y * _e345.y))));
        let _e362 = (_e347 + -12.8f);
        let _e367 = (_e177 - (_e362 * 0.1543801f));
        let _e376 = vec4<f32>(_e183, vec4<f32>().y, vec4<f32>().z, vec4<f32>().w);
        let _e381 = vec4<f32>(_e376.x, _e347, _e376.z, _e376.w);
        let _e386 = vec4<f32>(_e381.x, _e381.y, _e191, _e381.w);
        dynlex_interpolant_7465727261696e20706f736974696f6e = vec4<f32>(_e386.x, _e386.y, _e386.z, _e144.w);
        let _e396 = vec4<f32>((_e345.x / _e358), vec4<f32>().y, vec4<f32>().z, vec4<f32>().w);
        let _e401 = vec4<f32>(_e396.x, (_e345.y / _e358), _e396.z, _e396.w);
        let _e406 = vec4<f32>(_e401.x, _e401.y, (_e345.z / _e358), _e401.w);
        dynlex_interpolant_7465727261696e206e6f726d616c = vec4<f32>(_e406.x, _e406.y, _e406.z, _e147.y);
        let _e416 = vec4<f32>((_e343 + 0.5f), vec4<f32>().y, vec4<f32>().z, vec4<f32>().w);
        let _e421 = vec4<f32>(_e416.x, (_e341 + 0.5f), _e416.z, _e416.w);
        dynlex_interpolant_7465727261696e206d6174657269616c = vec4<f32>(_e421.x, _e421.y, _e339, _e421.w);
        let _e431 = vec4<f32>((_e179 / (_e143 * 0.72f)), vec4<f32>().y, vec4<f32>().z, vec4<f32>().w);
        let _e437 = vec4<f32>(_e431.x, (((_e176 * 0.1543801f) + (_e362 * 0.98801154f)) / -0.72f), _e431.z, _e431.w);
        let _e442 = vec4<f32>(_e437.x, _e437.y, ((_e367 * 1.00078f) + -0.40016f), _e437.w);
        phi_392_ = vec4<f32>(_e442.x, _e442.y, _e442.z, _e367);
    }
    let _e472 = phi_392_;
    gl_Position = _e472;
    return;
}

@vertex 
fn main(@location(0) dynlex_vertex_506f736974696f6e_0_4_4_4_: vec4<f32>) -> VertexOutput {
    dynlex_vertex_506f736974696f6e_0_4_4_4_1 = dynlex_vertex_506f736974696f6e_0_4_4_4_;
    main_1();
    let _e7 = gl_Position.y;
    gl_Position.y = -(_e7);
    let _e9 = gl_Position;
    let _e10 = dynlex_interpolant_7465727261696e20706f736974696f6e;
    let _e11 = dynlex_interpolant_7465727261696e206e6f726d616c;
    let _e12 = dynlex_interpolant_7465727261696e206d6174657269616c;
    return VertexOutput(_e9, _e10, _e11, _e12);
}
