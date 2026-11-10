struct type_6 {
    member: vec2<f32>,
}

struct type_11 {
    member: f32,
}

var<private> gl_FragCoord_1: vec4<f32>;
var<private> gl_FragColor: vec4<f32>;
@group(0) @binding(0) 
var<uniform> ubo_time: type_11;
@group(0) @binding(2) 
var<uniform> ubo_width: type_11;
@group(0) @binding(3) 
var<uniform> ubo_height: type_11;
@group(0) @binding(1) 
var<uniform> ubo_render_pass: type_11;

fn the_distance_from_3a_point8point5_to_segment_from_3a_point8start5_to_3a_point8end5_a_20_point_a_20_point_a_20_point(point: ptr<function, type_6>, start: ptr<function, type_6>, end: ptr<function, type_6>) -> f32 {
    var phi_750_: u32;
    var phi_752_: f32;

    let _e136 = (*end).member;
    let _e139 = (*start).member;
    let _e141 = (_e136.x - _e139.x);
    let _e144 = (_e136.y - _e139.y);
    let _e146 = (*point).member;
    let _e158 = (((_e141 * (_e146.x - _e139.x)) + (_e144 * (_e146.y - _e139.y))) / max(((_e141 * _e141) + (_e144 * _e144)), 0.000001f));
    if (_e158 < 0f) {
        phi_752_ = 0f;
    } else {
        phi_750_ = 0u;
        if (_e158 > 1f) {
        } else {
            phi_750_ = 1u;
        }
        let _e162 = phi_750_;
        phi_752_ = _e158;
        if (0u == _e162) {
            phi_752_ = 1f;
        }
    }
    let _e165 = phi_752_;
    let _e170 = (_e146.x - (_e139.x + (_e141 * _e165)));
    let _e171 = (_e146.y - (_e139.y + (_e144 * _e165)));
    return sqrt(((_e170 * _e170) + (_e171 * _e171)));
}

fn the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_20_point_f32_(point_1: ptr<function, type_6>, phase: ptr<function, f32>) -> f32 {
    let _e135 = (*point_1).member;
    let _e141 = (*phase);
    let _e151 = (_e135.x + (sin((_e141 + ((_e135.x * 0.73f) - (_e135.y * 0.41f)))) * 0.58f));
    let _e153 = (_e135.y + (cos((((_e135.x * 0.37f) + (_e135.y * 0.89f)) - (_e141 * 0.71f))) * 0.58f));
    let _e159 = sin(((_e141 * 0.43f) + ((_e151 * 1.31f) + (_e153 * 0.87f))));
    let _e165 = cos((((_e153 * 1.73f) - (_e151 * 0.79f)) - (_e141 * 0.31f)));
    return (((_e159 * 0.46f) + (_e165 * 0.29f)) + ((sin((((_e151 * 2.47f) - (_e153 * 2.11f)) + (_e159 * 1.8f))) * 0.17f) + (cos((((_e151 * 4.03f) + (_e153 * 3.17f)) + (_e165 * 1.4f))) * 0.08f)));
}

fn the_spark_field_at_3a_point8point5_during_3a_value8phase5_a_20_point_f32_(point_2: ptr<function, type_6>, phase_1: ptr<function, f32>) -> f32 {
    var tmp77_: f32;
    var tmp39_: f32;
    var location: type_6;
    var shifted: type_6;
    var sample: type_6;
    var phi_689_: u32;
    var phi_691_: f32;

    let _e140 = (*point_2).member;
    let _e145 = vec2<f32>((_e140.x * 0.19f), vec2<f32>().y);
    sample.member = vec2<f32>(_e145.x, (_e140.y * 0.19f));
    let _e152 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_20_point_f32_((&sample), phase_1);
    let _e157 = vec2<f32>(((_e140.x * 0.23f) + 7f), vec2<f32>().y);
    shifted.member = vec2<f32>(_e157.x, ((_e140.y * 0.23f) + -5f));
    let _e165 = (_e140.x + (_e152 * 1.7f));
    let _e166 = (*phase_1);
    tmp39_ = (_e166 + 1.7f);
    let _e168 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_20_point_f32_((&shifted), (&tmp39_));
    let _e170 = (_e140.y + (_e168 * 1.7f));
    let _e174 = vec2<f32>((_e165 * 0.11f), vec2<f32>().y);
    location.member = vec2<f32>(_e174.x, (_e170 * 0.11f));
    tmp77_ = (_e166 + 4f);
    let _e181 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_20_point_f32_((&location), (&tmp77_));
    let _e185 = ((((_e181 * 0.5f) + 0.5f) + -0.48f) / 0.34f);
    if (_e185 < 0f) {
        phi_691_ = 0f;
    } else {
        phi_689_ = 0u;
        if (_e185 > 1f) {
        } else {
            phi_689_ = 1u;
        }
        let _e189 = phi_689_;
        phi_691_ = _e185;
        if (0u == _e189) {
            phi_691_ = 1f;
        }
    }
    let _e192 = phi_691_;
    let _e197 = sin((_e166 + ((_e165 * 1.73f) + (_e170 * 0.31f))));
    let _e204 = sin((((_e170 * 1.91f) - (_e165 * 0.27f)) - (_e166 * 0.83f)));
    let _e208 = (sqrt(((_e197 * _e197) + (_e204 * _e204))) / 0.115f);
    let _e210 = select(_e208, 1f, (_e208 > 1f));
    return ((1f - ((_e210 * _e210) * (3f - (_e210 * 2f)))) * ((_e192 * _e192) * (3f - (_e192 * 2f))));
}

fn the_scene_window_from_opening_to_closing_at_moment_f32_f32_f32_(opening: ptr<function, f32>, closing: ptr<function, f32>, moment: ptr<function, f32>) -> f32 {
    var phi_567_: u32;
    var phi_569_: f32;
    var phi_577_: u32;
    var phi_579_: f32;

    let _e135 = (*opening);
    let _e137 = (*moment);
    let _e140 = ((_e137 - _e135) / ((_e135 + 0.55f) - _e135));
    if (_e140 < 0f) {
        phi_569_ = 0f;
    } else {
        phi_567_ = 0u;
        if (_e140 > 1f) {
        } else {
            phi_567_ = 1u;
        }
        let _e144 = phi_567_;
        phi_569_ = _e140;
        if (0u == _e144) {
            phi_569_ = 1f;
        }
    }
    let _e147 = phi_569_;
    let _e148 = (*closing);
    let _e149 = (_e148 + -0.55f);
    let _e152 = ((_e137 - _e149) / (_e148 - _e149));
    if (_e152 < 0f) {
        phi_579_ = 0f;
    } else {
        phi_577_ = 0u;
        if (_e152 > 1f) {
        } else {
            phi_577_ = 1u;
        }
        let _e156 = phi_577_;
        phi_579_ = _e152;
        if (0u == _e156) {
            phi_579_ = 1f;
        }
    }
    let _e159 = phi_579_;
    return (((_e147 * _e147) * (3f - (_e147 * 2f))) * (1f - ((_e159 * _e159) * (3f - (_e159 * 2f)))));
}

fn main_1() {
    var tmp238_: f32;
    var tmp237_: type_6;
    var tmp219_: f32;
    var tmp218_: type_6;
    var tmp197_: f32;
    var tmp196_: type_6;
    var tmp175_: f32;
    var tmp174_: type_6;
    var tmp153_: f32;
    var tmp152_: f32;
    var tmp150_: f32;
    var tmp149_: f32;
    var finish: type_6;
    var start_1: type_6;
    var ending: type_6;
    var center: type_6;
    var moment_1: f32;
    var screen: type_6;
    var phi_350_: u32;
    var phi_352_: f32;
    var phi_369_: u32;
    var phi_371_: f32;
    var phi_406_: u32;
    var phi_408_: f32;
    var phi_440_: u32;
    var phi_442_: f32;
    var phi_492_: u32;
    var phi_494_: f32;
    var phi_529_: u32;
    var phi_531_: f32;
    var phi_549_: f32;
    var phi_550_: f32;
    var phi_551_: f32;

    let _e150 = gl_FragCoord_1;
    let _e152 = gl_FragCoord_1;
    let _e155 = ubo_time.member;
    let _e157 = ubo_width.member;
    let _e158 = max(_e157, 1f);
    let _e160 = ubo_height.member;
    let _e161 = max(_e160, 1f);
    let _e163 = ubo_render_pass.member;
    let _e168 = ((_e158 / _e161) * (((_e150.x / _e158) * 2f) + -1f));
    let _e171 = vec2<f32>(_e168, vec2<f32>().y);
    let _e174 = (1f - ((_e152.y / _e161) * 2f));
    screen.member = vec2<f32>(_e171.x, _e174);
    let _e179 = (_e168 * _e168);
    let _e182 = sqrt((_e179 + (_e174 * _e174)));
    let _e183 = min(_e155, 10.4f);
    moment_1 = _e183;
    if (_e163 > 0.5f) {
        let _e476 = ((sin(((_e155 * 4.2f) + ((_e168 * 19f) - (_e174 * 13f)))) * 0.5f) + 0.5f);
        let _e484 = ((_e183 + -4.45f) / 2.9500003f);
        if (_e484 < 0f) {
            phi_531_ = 0f;
        } else {
            phi_529_ = 0u;
            if (_e484 > 1f) {
            } else {
                phi_529_ = 1u;
            }
            let _e488 = phi_529_;
            phi_531_ = _e484;
            if (0u == _e488) {
                phi_531_ = 1f;
            }
        }
        let _e491 = phi_531_;
        let _e496 = (1f - ((_e491 * _e491) * (3f - (_e491 * 2f))));
        phi_549_ = (((_e476 * 0.3f) + 0.66f) - (_e496 * 0.18f));
        phi_550_ = (((((sin(((_e174 * 47f) - (_e155 * 6.8f))) * 0.5f) + 0.5f) * 0.24f) + 0.3f) + (_e496 * 0.12f));
        phi_551_ = (((_e476 * 0.28f) + 0.08f) + (_e496 * 0.38f));
    } else {
        tmp149_ = 0f;
        tmp150_ = 7.4f;
        let _e185 = the_scene_window_from_opening_to_closing_at_moment_f32_f32_f32_((&tmp149_), (&tmp150_), (&moment_1));
        tmp152_ = 6.85f;
        tmp153_ = 11f;
        let _e186 = the_scene_window_from_opening_to_closing_at_moment_f32_f32_f32_((&tmp152_), (&tmp153_), (&moment_1));
        let _e192 = vec2<f32>(((_e155 * 0.021f) + (_e168 * 2.2f)), vec2<f32>().y);
        tmp174_.member = vec2<f32>(_e192.x, ((_e174 * 2.2f) - (_e155 * 0.014f)));
        tmp175_ = 1.9f;
        let _e200 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_20_point_f32_((&tmp174_), (&tmp175_));
        let _e202 = ((_e200 * 0.5f) + 0.5f);
        let _e208 = vec2<f32>(((_e168 * 4.1f) - (_e155 * 0.017f)), vec2<f32>().y);
        tmp196_.member = vec2<f32>(_e208.x, ((_e155 * 0.011f) + (_e174 * 4.1f)));
        tmp197_ = 5.4f;
        let _e216 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_20_point_f32_((&tmp196_), (&tmp197_));
        let _e219 = (1f - min(abs(_e216), 1f));
        let _e220 = (_e219 * _e219);
        let _e226 = vec2<f32>(((_e155 * 0.24f) + (_e168 * 27f)), vec2<f32>().y);
        tmp218_.member = vec2<f32>(_e226.x, ((_e174 * 27f) - (_e155 * 0.13f)));
        tmp219_ = 9.7f;
        let _e234 = the_spark_field_at_3a_point8point5_during_3a_value8phase5_a_20_point_f32_((&tmp218_), (&tmp219_));
        let _e240 = vec2<f32>(((_e155 * 8.4f) + (_e168 * 7f)), vec2<f32>().y);
        tmp237_.member = vec2<f32>(_e240.x, (_e174 * 46f));
        tmp238_ = 14.2f;
        let _e246 = the_spark_field_at_3a_point8point5_during_3a_value8phase5_a_20_point_f32_((&tmp237_), (&tmp238_));
        let _e248 = (abs(_e174) / 0.66f);
        let _e250 = select(_e248, 1f, (_e248 > 1f));
        let _e257 = (_e185 * ((1f - ((_e250 * _e250) * (3f - (_e250 * 2f)))) * _e246));
        let _e283 = ((_e182 + -0.28f) / 0.98f);
        if (_e283 < 0f) {
            phi_352_ = 0f;
        } else {
            phi_350_ = 0u;
            if (_e283 > 1f) {
            } else {
                phi_350_ = 1u;
            }
            let _e287 = phi_350_;
            phi_352_ = _e283;
            if (0u == _e287) {
                phi_352_ = 1f;
            }
        }
        let _e290 = phi_352_;
        let _e296 = (_e186 * (1f - ((_e290 * _e290) * (3f - (_e290 * 2f)))));
        let _e304 = ((_e183 + -7.55f) / 0.5500002f);
        if (_e304 < 0f) {
            phi_371_ = 0f;
        } else {
            phi_369_ = 0u;
            if (_e304 > 1f) {
            } else {
                phi_369_ = 1u;
            }
            let _e308 = phi_369_;
            phi_371_ = _e304;
            if (0u == _e308) {
                phi_371_ = 1f;
            }
        }
        let _e311 = phi_371_;
        let _e317 = ((_e155 + -7.55f) * 1.32f);
        let _e319 = (cos(_e317) * 1.08f);
        let _e324 = (sin((_e155 * 0.31f)) * 0.34f);
        let _e331 = (((_e319 * -(sin(_e324))) * 0.52f) + 3.25f);
        center.member = vec2<f32>(0f, -0.012307692f);
        let _e334 = (((_e319 * cos(_e324)) * 1.66f) / _e331);
        let _e337 = vec2<f32>(_e334, vec2<f32>().y);
        let _e340 = ((((sin(_e317) * 1.08f) * 1.86f) + -0.04f) / _e331);
        ending.member = vec2<f32>(_e337.x, _e340);
        let _e345 = the_distance_from_3a_point8point5_to_segment_from_3a_point8start5_to_3a_point8end5_a_20_point_a_20_point_a_20_point((&screen), (&center), (&ending));
        let _e347 = ((_e345 + -0.002f) / 0.0069999993f);
        if (_e347 < 0f) {
            phi_408_ = 0f;
        } else {
            phi_406_ = 0u;
            if (_e347 > 1f) {
            } else {
                phi_406_ = 1u;
            }
            let _e351 = phi_406_;
            phi_408_ = _e347;
            if (0u == _e351) {
                phi_408_ = 1f;
            }
        }
        let _e354 = phi_408_;
        let _e360 = (_e340 + 0.012307692f);
        let _e365 = max(sqrt(((_e334 * _e334) + (_e360 * _e360))), 0.0001f);
        let _e369 = ((-(_e360) / _e365) * 0.026f);
        let _e373 = vec2<f32>((_e334 - _e369), vec2<f32>().y);
        let _e374 = ((_e334 / _e365) * 0.026f);
        start_1.member = vec2<f32>(_e373.x, (_e340 - _e374));
        let _e383 = vec2<f32>((_e334 + _e369), vec2<f32>().y);
        finish.member = vec2<f32>(_e383.x, (_e340 + _e374));
        let _e389 = the_distance_from_3a_point8point5_to_segment_from_3a_point8start5_to_3a_point8end5_a_20_point_a_20_point_a_20_point((&screen), (&start_1), (&finish));
        let _e391 = ((_e389 + -0.002f) / 0.009f);
        if (_e391 < 0f) {
            phi_442_ = 0f;
        } else {
            phi_440_ = 0u;
            if (_e391 > 1f) {
            } else {
                phi_440_ = 1u;
            }
            let _e395 = phi_440_;
            phi_442_ = _e391;
            if (0u == _e395) {
                phi_442_ = 1f;
            }
        }
        let _e398 = phi_442_;
        let _e404 = (_e168 - _e334);
        let _e405 = (_e174 - _e340);
        let _e410 = (sqrt(((_e404 * _e404) + (_e405 * _e405))) / 0.045f);
        let _e412 = select(_e410, 1f, (_e410 > 1f));
        let _e418 = (_e174 + 0.012307692f);
        let _e422 = (sqrt((_e179 + (_e418 * _e418))) / 0.025f);
        let _e424 = select(_e422, 1f, (_e422 > 1f));
        let _e437 = (((_e311 * _e311) * (3f - (_e311 * 2f))) * (((1f - ((_e424 * _e424) * (3f - (_e424 * 2f)))) * 0.36f) + (((1f - ((_e412 * _e412) * (3f - (_e412 * 2f)))) * 0.48f) + (((1f - ((_e354 * _e354) * (3f - (_e354 * 2f)))) * 0.62f) + ((1f - ((_e398 * _e398) * (3f - (_e398 * 2f)))) * 0.82f)))));
        let _e439 = (((((_e234 * 0.18f) + (((_e202 * 0.012f) + 0.003f) + (_e220 * 0.018f))) + (_e257 * 0.32f)) + (_e296 * 0.032f)) + (_e437 * 0.46f));
        let _e441 = (((((_e234 * 0.46f) + (((_e202 * 0.024f) + 0.006f) + (_e220 * 0.015f))) + (_e257 * 0.78f)) + (_e296 * 0.095f)) + (_e437 * 0.88f));
        let _e443 = (((((_e234 * 1.02f) + (((_e202 * 0.082f) + 0.024f) + (_e220 * 0.071f))) + (_e257 * 1.46f)) + (_e296 * 0.21f)) + (_e437 * 1.24f));
        let _e445 = ((_e182 + -0.52f) / 1.1f);
        if (_e445 < 0f) {
            phi_494_ = 0f;
        } else {
            phi_492_ = 0u;
            if (_e445 > 1f) {
            } else {
                phi_492_ = 1u;
            }
            let _e449 = phi_492_;
            phi_494_ = _e445;
            if (0u == _e449) {
                phi_494_ = 1f;
            }
        }
        let _e452 = phi_494_;
        let _e459 = (((1f - ((_e452 * _e452) * (3f - (_e452 * 2f)))) * 0.82f) + 0.18f);
        phi_549_ = ((_e443 * _e459) / (_e443 + 1f));
        phi_550_ = ((_e441 * _e459) / (_e441 + 1f));
        phi_551_ = ((_e439 * _e459) / (_e439 + 1f));
    }
    let _e510 = phi_549_;
    let _e512 = phi_550_;
    let _e514 = phi_551_;
    let _e519 = vec4<f32>(_e514, vec4<f32>(f32(), f32(), f32(), 1f).y, vec4<f32>(f32(), f32(), f32(), 1f).z, vec4<f32>(f32(), f32(), f32(), 1f).w);
    let _e524 = vec4<f32>(_e519.x, _e512, _e519.z, _e519.w);
    gl_FragColor = vec4<f32>(_e524.x, _e524.y, _e510, _e524.w);
    return;
}

@fragment 
fn main(@builtin(position) gl_FragCoord: vec4<f32>) -> @location(0) vec4<f32> {
    gl_FragCoord_1 = gl_FragCoord;
    main_1();
    let _e3 = gl_FragColor;
    return _e3;
}
