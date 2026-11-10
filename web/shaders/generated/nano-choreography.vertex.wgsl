struct type_8 {
    member: f32,
}

var<private> gl_Position: vec4<f32> = vec4<f32>(0f, 0f, 0f, 1f);
var<private> dynlex_vertex_506f736974696f6e_0_4_4_4_1: vec4<f32>;
@group(0) @binding(0) 
var<uniform> ubo_time: type_8;
@group(0) @binding(2) 
var<uniform> ubo_width: type_8;
@group(0) @binding(3) 
var<uniform> ubo_height: type_8;
@group(0) @binding(1) 
var<uniform> ubo_render_pass: type_8;

fn main_1() {
    var phi_139_: u32;
    var phi_141_: f32;
    var phi_146_: u32;
    var phi_148_: f32;
    var phi_152_: u32;
    var phi_154_: f32;
    var phi_171_: u32;
    var phi_173_: f32;

    let _e70 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
    let _e72 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
    let _e74 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
    let _e76 = dynlex_vertex_506f736974696f6e_0_4_4_4_1;
    let _e78 = floor((_e70.x * 0.00024414063f));
    let _e80 = floor((_e72.y * 0.00024414063f));
    let _e82 = floor((_e74.z * 0.00024414063f));
    let _e85 = (((_e78 / 4095f) * 4f) + -2f);
    let _e88 = (((_e80 / 4095f) * 4f) + -2f);
    let _e91 = (((_e82 / 4095f) * 4f) + -2f);
    let _e93 = ubo_time.member;
    let _e95 = ubo_width.member;
    let _e97 = ubo_height.member;
    let _e99 = ubo_render_pass.member;
    let _e100 = min(_e93, 10.4f);
    let _e101 = (_e100 / 0.55f);
    if (_e101 < 0f) {
        phi_141_ = 0f;
    } else {
        phi_139_ = 0u;
        if (_e101 > 1f) {
        } else {
            phi_139_ = 1u;
        }
        let _e105 = phi_139_;
        phi_141_ = _e101;
        if (0u == _e105) {
            phi_141_ = 1f;
        }
    }
    let _e108 = phi_141_;
    let _e110 = ((_e100 + -10.45f) / 0.5500002f);
    if (_e110 < 0f) {
        phi_148_ = 0f;
    } else {
        phi_146_ = 0u;
        if (_e110 > 1f) {
        } else {
            phi_146_ = 1u;
        }
        let _e114 = phi_146_;
        phi_148_ = _e110;
        if (0u == _e114) {
            phi_148_ = 1f;
        }
    }
    let _e117 = phi_148_;
    let _e118 = (_e100 / 4.25f);
    if (_e118 < 0f) {
        phi_154_ = 0f;
    } else {
        phi_152_ = 0u;
        if (_e118 > 1f) {
        } else {
            phi_152_ = 1u;
        }
        let _e122 = phi_152_;
        phi_154_ = _e118;
        if (0u == _e122) {
            phi_154_ = 1f;
        }
    }
    let _e125 = phi_154_;
    let _e134 = (((sin((((_e85 * 3.13f) + (_e88 * 2.71f)) + (_e91 * 4.19f))) * 0.5f) + 0.5f) * 0.75f);
    let _e135 = (_e134 + 4.45f);
    let _e139 = ((_e100 - _e135) / ((_e134 + 6.65f) - _e135));
    if (_e139 < 0f) {
        phi_173_ = 0f;
    } else {
        phi_171_ = 0u;
        if (_e139 > 1f) {
        } else {
            phi_171_ = 1u;
        }
        let _e143 = phi_171_;
        phi_173_ = _e139;
        if (0u == _e143) {
            phi_173_ = 1f;
        }
    }
    let _e146 = phi_173_;
    let _e154 = ((sin((((_e88 * 3.91f) - (_e85 * 2.17f)) + (_e91 * 2.43f))) * 0.5f) + 0.5f);
    let _e159 = (sin((_e93 * 0.31f)) * 0.34f);
    let _e160 = cos(_e159);
    let _e162 = sin(_e159);
    let _e166 = ((((_e91 * _e160) - (_e85 * _e162)) * 0.52f) + 3.25f);
    let _e175 = (1f / (max(_e95, 1f) / max(_e97, 1f)));
    let _e180 = floor((_e76.w * 0.25f));
    let _e186 = ((((_e70.x - (_e78 * 4096f)) / 4095f) * 4f) + -2f);
    let _e190 = vec3<f32>(_e186, vec3<f32>().y, vec3<f32>().z);
    let _e195 = ((((_e72.y - (_e80 * 4096f)) / 4095f) * 4f) + -2f);
    let _e199 = vec3<f32>(_e190.x, _e195, _e190.z);
    let _e208 = vec3<f32>(_e199.x, _e199.y, ((((_e74.z - (_e82 * 4096f)) / 4095f) * 4f) + -2f));
    let _e210 = select(-0.72f, 0.72f, (_e186 > 0f));
    let _e211 = (_e186 - _e210);
    let _e212 = (_e93 * -9.4f);
    let _e213 = cos(_e212);
    let _e215 = (_e195 + 0.42f);
    let _e216 = sin(_e212);
    let _e223 = vec3<f32>((_e210 + ((_e213 * _e211) - (_e216 * _e215))), _e208.y, _e208.z);
    let _e232 = select(_e208, vec3<f32>(_e223.x, (((_e213 * _e215) + (_e216 * _e211)) + -0.42f), _e223.z), (_e180 > 0.5f));
    let _e237 = ((_e125 * _e125) * (3f - (_e125 * 2f)));
    let _e244 = ((_e237 * 0.04f) + 1.3f);
    let _e245 = cos(_e244);
    let _e248 = sin(_e244);
    let _e255 = max((((2.8f - (_e237 * 4.9f)) + ((_e232.z * _e245) - (_e232.x * _e248))) + 4.2f), 0.2f);
    let _e256 = (((_e232.y + ((_e237 * 0.12f) + -0.19f)) * 1.72f) / _e255);
    let _e265 = ((_e175 * ((((_e237 * 1.18f) + -1.26f) + ((_e232.x * _e245) + (_e232.z * _e248))) * 1.72f)) / _e255);
    let _e277 = (_e76.w - (_e180 * 4f));
    let _e281 = ((_e146 * _e146) * (3f - (_e146 * 2f)));
    let _e283 = sin((_e281 * 3.1415927f));
    let _e286 = (((_e154 * 0.24f) + 0.1f) * _e283);
    let _e289 = ((_e154 * 6.2831855f) + (_e281 * 3.2f));
    let _e308 = (_e255 + ((_e166 - _e255) * _e281));
    let _e311 = (((_e99 * 0.04f) + 0.96f) * 0.00104f);
    let _e312 = (_e175 * _e311);
    let _e316 = vec2<f32>(-(_e312), vec2<f32>().y);
    let _e320 = vec2<f32>(_e316.x, -(_e311));
    let _e331 = select(select(_e320, vec2<f32>(_e312, _e320.y), (_e277 >= 1f)), vec2<f32>(vec2<f32>(0f, f32()).x, (_e311 * 1.45f)), (_e277 >= 2f));
    let _e337 = vec2<f32>((_e308 * (_e331.x + ((_e265 + ((((_e175 * (((_e85 * _e160) + (_e91 * _e162)) * 1.66f)) / _e166) - _e265) * _e281)) + (_e175 * ((_e283 * 0.12f) + (cos(_e289) * _e286)))))), vec2<f32>().y);
    let _e343 = vec2<f32>(_e337.x, (_e308 * (_e331.y + ((_e256 + (((((_e88 * 1.86f) + -0.04f) / _e166) - _e256) * _e281)) + ((sin(_e289) * _e286) * 0.72f)))));
    let _e349 = select(_e343, vec2<f32>((_e308 * 3f), _e343.y), ((((_e108 * _e108) * (3f - (_e108 * 2f))) * (1f - ((_e117 * _e117) * (3f - (_e117 * 2f))))) < 0.001f));
    let _e355 = vec4<f32>(_e349.x, vec4<f32>().y, vec4<f32>().z, vec4<f32>().w);
    let _e362 = vec4<f32>(_e355.x, -(_e349.y), _e355.z, _e355.w);
    let _e368 = vec4<f32>(_e362.x, _e362.y, (_e308 * 0.42f), _e362.w);
    gl_Position = vec4<f32>(_e368.x, _e368.y, _e368.z, _e308);
    return;
}

@vertex 
fn main(@location(0) dynlex_vertex_506f736974696f6e_0_4_4_4_: vec4<f32>) -> @builtin(position) vec4<f32> {
    dynlex_vertex_506f736974696f6e_0_4_4_4_1 = dynlex_vertex_506f736974696f6e_0_4_4_4_;
    main_1();
    let _e4 = gl_Position.y;
    gl_Position.y = -(_e4);
    let _e6 = gl_Position;
    return _e6;
}
