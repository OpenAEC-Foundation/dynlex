#version 300 es

struct class_0
{
    vec2 _m0;
};

struct _class
{
    vec3 _m0;
};

layout(std140) uniform DynlexUniformBlock0
{
    float value;
} dynlexUniform0;

layout(std140) uniform DynlexUniformBlock2
{
    float value;
} dynlexUniform2;

layout(std140) uniform DynlexUniformBlock3
{
    float value;
} dynlexUniform3;

layout(std140) uniform DynlexUniformBlock1
{
    float value;
} dynlexUniform1;

layout(location = 0) in vec4 in_Position;

float left1_4371_43right_f32_f32(float left, float right)
{
    return left / right;
}

float the_floor_of_value_f32(float value)
{
    return floor(value);
}

float left1_4321_43right_f32_f32(float left, float right)
{
    return left * right;
}

float left1_4351_43right_f32_f32(float left, float right)
{
    return left - right;
}

float the_maximum_of_left_and_right_f32_f32(float left, float right)
{
    return isnan(right) ? left : (isnan(left) ? right : max(left, right));
}

float the_minimum_of_left_and_right_f32_f32(float left, float right)
{
    return isnan(right) ? left : (isnan(left) ? right : min(left, right));
}

float left1_4331_43right_f32_f32(float left, float right)
{
    return left + right;
}

bool left_0_right_f32_f32(float left, float right)
{
    return left < right;
}

bool left_2_right_f32_f32(float left, float right)
{
    return left > right;
}

float number_saturated_f32(float number)
{
    float result = number;
    float tmp = 0.0;
    if (left_0_right_f32_f32(result, tmp))
    {
        result = 0.0;
    }
    float tmp3 = 1.0;
    if (left_2_right_f32_f32(result, tmp3))
    {
        result = 1.0;
    }
    return result;
}

float the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(float lower, float upper, float _sample)
{
    float tmp = left1_4351_43right_f32_f32(_sample, lower);
    float tmp1 = left1_4351_43right_f32_f32(upper, lower);
    float normalized = left1_4371_43right_f32_f32(tmp, tmp1);
    normalized = number_saturated_f32(normalized);
    float tmp2 = left1_4321_43right_f32_f32(normalized, normalized);
    float tmp3 = 3.0;
    float tmp4 = 2.0;
    float tmp5 = left1_4321_43right_f32_f32(tmp4, normalized);
    float tmp6 = left1_4351_43right_f32_f32(tmp3, tmp5);
    return left1_4321_43right_f32_f32(tmp2, tmp6);
}

float the_scene_window_from_opening_to_closing_at_moment_f32_f32_f32(float opening, float closing, float moment)
{
    float tmp = 0.550000011920928955078125;
    float tmp1 = left1_4331_43right_f32_f32(opening, tmp);
    float arrival = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(opening, tmp1, moment);
    float tmp2 = 1.0;
    float tmp3 = 0.550000011920928955078125;
    float tmp4 = left1_4351_43right_f32_f32(closing, tmp3);
    float tmp5 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp4, closing, moment);
    float departure = left1_4351_43right_f32_f32(tmp2, tmp5);
    return left1_4321_43right_f32_f32(arrival, departure);
}

float the_sine_of_value_f32(float value)
{
    return sin(value);
}

float the_cosine_of_value_f32(float value)
{
    return cos(value);
}

float _the_negative_of_4the_opposite_of_453value_f32(float value)
{
    return -value;
}

bool left_1is_greater_than_or_equal_to4213_right_f32_f32(float left, float right)
{
    return left >= right;
}

void main()
{
    vec3 _434 = vec3(0.0);
    _434.x = in_Position.x;
    _434.y = in_Position.y;
    _434.z = in_Position.z;
    _class class_tmp = _class(vec3(0.0));
    class_tmp._m0 = _434;
    _class _packed = class_tmp;
    float encoding = in_Position.w;
    float tmp = 4.0;
    float tmp9 = left1_4371_43right_f32_f32(encoding, tmp);
    float wheel = the_floor_of_value_f32(tmp9);
    float tmp10 = 4.0;
    float tmp11 = left1_4321_43right_f32_f32(wheel, tmp10);
    float triangle = left1_4351_43right_f32_f32(encoding, tmp11);
    float tmp15 = _packed._m0.x;
    float tmp16 = 4096.0;
    float tmp17 = left1_4371_43right_f32_f32(tmp15, tmp16);
    vec3 _454 = vec3(0.0);
    _454.x = the_floor_of_value_f32(tmp17);
    float tmp22 = _packed._m0.y;
    float tmp23 = 4096.0;
    float tmp24 = left1_4371_43right_f32_f32(tmp22, tmp23);
    _454.y = the_floor_of_value_f32(tmp24);
    float tmp29 = _packed._m0.z;
    float tmp30 = 4096.0;
    float tmp31 = left1_4371_43right_f32_f32(tmp29, tmp30);
    _454.z = the_floor_of_value_f32(tmp31);
    _class class_tmp12 = _class(vec3(0.0));
    class_tmp12._m0 = _454;
    _class target = class_tmp12;
    float tmp39 = _packed._m0.x;
    float tmp43 = target._m0.x;
    float tmp44 = 4096.0;
    float tmp45 = left1_4321_43right_f32_f32(tmp43, tmp44);
    vec3 _477 = vec3(0.0);
    _477.x = left1_4351_43right_f32_f32(tmp39, tmp45);
    float tmp50 = _packed._m0.y;
    float tmp54 = target._m0.y;
    float tmp55 = 4096.0;
    float tmp56 = left1_4321_43right_f32_f32(tmp54, tmp55);
    _477.y = left1_4351_43right_f32_f32(tmp50, tmp56);
    float tmp61 = _packed._m0.z;
    float tmp65 = target._m0.z;
    float tmp66 = 4096.0;
    float tmp67 = left1_4321_43right_f32_f32(tmp65, tmp66);
    _477.z = left1_4351_43right_f32_f32(tmp61, tmp67);
    _class class_tmp35 = _class(vec3(0.0));
    class_tmp35._m0 = _477;
    _class quantized = class_tmp35;
    float tmp75 = target._m0.x;
    float tmp76 = 4095.0;
    float tmp77 = left1_4371_43right_f32_f32(tmp75, tmp76);
    float tmp78 = 4.0;
    float tmp79 = left1_4321_43right_f32_f32(tmp77, tmp78);
    float tmp80 = 2.0;
    vec3 _504 = vec3(0.0);
    _504.x = left1_4351_43right_f32_f32(tmp79, tmp80);
    float tmp85 = target._m0.y;
    float tmp86 = 4095.0;
    float tmp87 = left1_4371_43right_f32_f32(tmp85, tmp86);
    float tmp88 = 4.0;
    float tmp89 = left1_4321_43right_f32_f32(tmp87, tmp88);
    float tmp90 = 2.0;
    _504.y = left1_4351_43right_f32_f32(tmp89, tmp90);
    float tmp95 = target._m0.z;
    float tmp96 = 4095.0;
    float tmp97 = left1_4371_43right_f32_f32(tmp95, tmp96);
    float tmp98 = 4.0;
    float tmp99 = left1_4321_43right_f32_f32(tmp97, tmp98);
    float tmp100 = 2.0;
    _504.z = left1_4351_43right_f32_f32(tmp99, tmp100);
    _class class_tmp71 = _class(vec3(0.0));
    class_tmp71._m0 = _504;
    _class point = class_tmp71;
    float tmp108 = quantized._m0.x;
    float tmp109 = 4095.0;
    float tmp110 = left1_4371_43right_f32_f32(tmp108, tmp109);
    float tmp111 = 4.0;
    float tmp112 = left1_4321_43right_f32_f32(tmp110, tmp111);
    float tmp113 = 2.0;
    vec3 _527 = vec3(0.0);
    _527.x = left1_4351_43right_f32_f32(tmp112, tmp113);
    float tmp118 = quantized._m0.y;
    float tmp119 = 4095.0;
    float tmp120 = left1_4371_43right_f32_f32(tmp118, tmp119);
    float tmp121 = 4.0;
    float tmp122 = left1_4321_43right_f32_f32(tmp120, tmp121);
    float tmp123 = 2.0;
    _527.y = left1_4351_43right_f32_f32(tmp122, tmp123);
    float tmp128 = quantized._m0.z;
    float tmp129 = 4095.0;
    float tmp130 = left1_4371_43right_f32_f32(tmp128, tmp129);
    float tmp131 = 4.0;
    float tmp132 = left1_4321_43right_f32_f32(tmp130, tmp131);
    float tmp133 = 2.0;
    _527.z = left1_4351_43right_f32_f32(tmp132, tmp133);
    _class class_tmp104 = _class(vec3(0.0));
    class_tmp104._m0 = _527;
    _class local = class_tmp104;
    float time = dynlexUniform0.value;
    float tmp138 = dynlexUniform2.value;
    float tmp139 = 1.0;
    vec2 _547 = vec2(0.0);
    _547.x = the_maximum_of_left_and_right_f32_f32(tmp138, tmp139);
    float tmp141 = dynlexUniform3.value;
    float tmp142 = 1.0;
    _547.y = the_maximum_of_left_and_right_f32_f32(tmp141, tmp142);
    class_0 class_tmp137 = class_0(vec2(0.0));
    class_tmp137._m0 = _547;
    class_0 frame = class_tmp137;
    float pass = dynlexUniform1.value;
    float tmp149 = frame._m0.x;
    float tmp153 = frame._m0.y;
    float aspect = left1_4371_43right_f32_f32(tmp149, tmp153);
    float tmp154 = 1.0;
    float scale = left1_4371_43right_f32_f32(tmp154, aspect);
    float tmp155 = 10.3999996185302734375;
    float moment = the_minimum_of_left_and_right_f32_f32(time, tmp155);
    float tmp156 = 0.0;
    float tmp157 = 11.0;
    float visibility = the_scene_window_from_opening_to_closing_at_moment_f32_f32_f32(tmp156, tmp157, moment);
    float tmp158 = 0.0;
    float tmp159 = left1_4351_43right_f32_f32(tmp158, time);
    float tmp160 = 9.3999996185302734375;
    float spin = left1_4321_43right_f32_f32(tmp159, tmp160);
    float sine = the_sine_of_value_f32(spin);
    float cosine = the_cosine_of_value_f32(spin);
    float tmp161 = 0.7200000286102294921875;
    float center = _the_negative_of_4the_opposite_of_453value_f32(tmp161);
    float tmp165 = local._m0.x;
    float tmp166 = 0.0;
    if (left_2_right_f32_f32(tmp165, tmp166))
    {
        center = 0.7200000286102294921875;
    }
    float tmp171 = local._m0.x;
    vec2 _577 = vec2(0.0);
    _577.x = left1_4351_43right_f32_f32(tmp171, center);
    float tmp176 = local._m0.y;
    float tmp177 = 0.4199999868869781494140625;
    _577.y = left1_4331_43right_f32_f32(tmp176, tmp177);
    class_0 class_tmp167 = class_0(vec2(0.0));
    class_tmp167._m0 = _577;
    class_0 offset = class_tmp167;
    float tmp185 = offset._m0.x;
    float tmp186 = left1_4321_43right_f32_f32(tmp185, cosine);
    float tmp190 = offset._m0.y;
    float tmp191 = left1_4321_43right_f32_f32(tmp190, sine);
    vec2 _594 = vec2(0.0);
    _594.x = left1_4351_43right_f32_f32(tmp186, tmp191);
    float tmp196 = offset._m0.x;
    float tmp197 = left1_4321_43right_f32_f32(tmp196, sine);
    float tmp201 = offset._m0.y;
    float tmp202 = left1_4321_43right_f32_f32(tmp201, cosine);
    _594.y = left1_4331_43right_f32_f32(tmp197, tmp202);
    class_0 class_tmp181 = class_0(vec2(0.0));
    class_tmp181._m0 = _594;
    class_0 rotated = class_tmp181;
    float tmp208 = 0.5;
    if (left_2_right_f32_f32(wheel, tmp208))
    {
        vec3 _609 = local._m0;
        float tmp214 = rotated._m0.x;
        _609.x = left1_4331_43right_f32_f32(center, tmp214);
        local._m0 = _609;
        vec3 _617 = local._m0;
        float tmp218 = 0.4199999868869781494140625;
        float tmp219 = _the_negative_of_4the_opposite_of_453value_f32(tmp218);
        float tmp223 = rotated._m0.y;
        _617.y = left1_4331_43right_f32_f32(tmp219, tmp223);
        local._m0 = _617;
    }
    float tmp226 = 0.0;
    float tmp227 = 4.25;
    float progress = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp226, tmp227, moment);
    float tmp228 = 1.2999999523162841796875;
    float tmp229 = 0.039999999105930328369140625;
    float tmp230 = left1_4321_43right_f32_f32(progress, tmp229);
    float yaw = left1_4331_43right_f32_f32(tmp228, tmp230);
    sine = the_sine_of_value_f32(yaw);
    cosine = the_cosine_of_value_f32(yaw);
    float tmp235 = local._m0.x;
    float tmp236 = left1_4321_43right_f32_f32(tmp235, cosine);
    float tmp240 = local._m0.z;
    float tmp241 = left1_4321_43right_f32_f32(tmp240, sine);
    vec3 _639 = vec3(0.0);
    _639.x = left1_4331_43right_f32_f32(tmp236, tmp241);
    _639.y = local._m0.y;
    float tmp250 = local._m0.z;
    float tmp251 = left1_4321_43right_f32_f32(tmp250, cosine);
    float tmp255 = local._m0.x;
    float tmp256 = left1_4321_43right_f32_f32(tmp255, sine);
    _639.z = left1_4351_43right_f32_f32(tmp251, tmp256);
    _class class_tmp231 = _class(vec3(0.0));
    class_tmp231._m0 = _639;
    _class turned = class_tmp231;
    float tmp264 = turned._m0.x;
    float tmp265 = 0.0;
    float tmp266 = 1.2599999904632568359375;
    float tmp267 = left1_4351_43right_f32_f32(tmp265, tmp266);
    float tmp268 = 1.17999994754791259765625;
    float tmp269 = left1_4321_43right_f32_f32(progress, tmp268);
    float tmp270 = left1_4331_43right_f32_f32(tmp267, tmp269);
    vec3 _663 = vec3(0.0);
    _663.x = left1_4331_43right_f32_f32(tmp264, tmp270);
    float tmp275 = turned._m0.y;
    float tmp276 = 0.0;
    float tmp277 = 0.189999997615814208984375;
    float tmp278 = left1_4351_43right_f32_f32(tmp276, tmp277);
    float tmp279 = 0.119999997317790985107421875;
    float tmp280 = left1_4321_43right_f32_f32(progress, tmp279);
    float tmp281 = left1_4331_43right_f32_f32(tmp278, tmp280);
    _663.y = left1_4331_43right_f32_f32(tmp275, tmp281);
    float tmp286 = turned._m0.z;
    float tmp287 = 2.7999999523162841796875;
    float tmp288 = 4.900000095367431640625;
    float tmp289 = left1_4321_43right_f32_f32(progress, tmp288);
    float tmp290 = left1_4351_43right_f32_f32(tmp287, tmp289);
    _663.z = left1_4331_43right_f32_f32(tmp286, tmp290);
    _class class_tmp260 = _class(vec3(0.0));
    class_tmp260._m0 = _663;
    _class world = class_tmp260;
    float tmp297 = world._m0.z;
    float tmp298 = 4.19999980926513671875;
    float tmp299 = left1_4331_43right_f32_f32(tmp297, tmp298);
    float tmp300 = 0.20000000298023223876953125;
    float depth = the_maximum_of_left_and_right_f32_f32(tmp299, tmp300);
    float tmp305 = world._m0.x;
    float tmp306 = 1.7200000286102294921875;
    float tmp307 = left1_4321_43right_f32_f32(tmp305, tmp306);
    float tmp308 = left1_4321_43right_f32_f32(tmp307, scale);
    vec2 _692 = vec2(0.0);
    _692.x = left1_4371_43right_f32_f32(tmp308, depth);
    float tmp313 = world._m0.y;
    float tmp314 = 1.7200000286102294921875;
    float tmp315 = left1_4321_43right_f32_f32(tmp313, tmp314);
    _692.y = left1_4371_43right_f32_f32(tmp315, depth);
    class_0 class_tmp301 = class_0(vec2(0.0));
    class_tmp301._m0 = _692;
    class_0 motorcycle = class_tmp301;
    float tmp319 = 0.310000002384185791015625;
    float tmp320 = left1_4321_43right_f32_f32(time, tmp319);
    float tmp321 = the_sine_of_value_f32(tmp320);
    float tmp322 = 0.3400000035762786865234375;
    yaw = left1_4321_43right_f32_f32(tmp321, tmp322);
    sine = the_sine_of_value_f32(yaw);
    cosine = the_cosine_of_value_f32(yaw);
    float tmp327 = point._m0.x;
    float tmp328 = left1_4321_43right_f32_f32(tmp327, cosine);
    float tmp332 = point._m0.z;
    float tmp333 = left1_4321_43right_f32_f32(tmp332, sine);
    vec3 _715 = vec3(0.0);
    _715.x = left1_4331_43right_f32_f32(tmp328, tmp333);
    _715.y = point._m0.y;
    float tmp342 = point._m0.z;
    float tmp343 = left1_4321_43right_f32_f32(tmp342, cosine);
    float tmp347 = point._m0.x;
    float tmp348 = left1_4321_43right_f32_f32(tmp347, sine);
    _715.z = left1_4351_43right_f32_f32(tmp343, tmp348);
    _class class_tmp323 = _class(vec3(0.0));
    class_tmp323._m0 = _715;
    turned = class_tmp323;
    float tmp352 = 3.25;
    float tmp356 = turned._m0.z;
    float tmp357 = 0.519999980926513671875;
    float tmp358 = left1_4321_43right_f32_f32(tmp356, tmp357);
    float destination = left1_4331_43right_f32_f32(tmp352, tmp358);
    float tmp363 = turned._m0.x;
    float tmp364 = 1.65999996662139892578125;
    float tmp365 = left1_4321_43right_f32_f32(tmp363, tmp364);
    float tmp366 = left1_4321_43right_f32_f32(tmp365, scale);
    vec2 _743 = vec2(0.0);
    _743.x = left1_4371_43right_f32_f32(tmp366, destination);
    float tmp371 = point._m0.y;
    float tmp372 = 1.86000001430511474609375;
    float tmp373 = left1_4321_43right_f32_f32(tmp371, tmp372);
    float tmp374 = 0.039999999105930328369140625;
    float tmp375 = left1_4351_43right_f32_f32(tmp373, tmp374);
    _743.y = left1_4371_43right_f32_f32(tmp375, destination);
    class_0 class_tmp359 = class_0(vec2(0.0));
    class_tmp359._m0 = _743;
    class_0 projection = class_tmp359;
    float tmp382 = point._m0.x;
    float tmp383 = 3.13000011444091796875;
    float tmp384 = left1_4321_43right_f32_f32(tmp382, tmp383);
    float tmp388 = point._m0.y;
    float tmp389 = 2.71000003814697265625;
    float tmp390 = left1_4321_43right_f32_f32(tmp388, tmp389);
    float tmp391 = left1_4331_43right_f32_f32(tmp384, tmp390);
    float tmp395 = point._m0.z;
    float tmp396 = 4.190000057220458984375;
    float tmp397 = left1_4321_43right_f32_f32(tmp395, tmp396);
    float tmp398 = left1_4331_43right_f32_f32(tmp391, tmp397);
    float tmp399 = the_sine_of_value_f32(tmp398);
    float tmp400 = 0.5;
    float tmp401 = left1_4321_43right_f32_f32(tmp399, tmp400);
    float tmp402 = 0.5;
    float seed = left1_4331_43right_f32_f32(tmp401, tmp402);
    float tmp406 = point._m0.x;
    float tmp407 = 2.1700000762939453125;
    float tmp408 = _the_negative_of_4the_opposite_of_453value_f32(tmp407);
    float tmp409 = left1_4321_43right_f32_f32(tmp406, tmp408);
    float tmp413 = point._m0.y;
    float tmp414 = 3.9100000858306884765625;
    float tmp415 = left1_4321_43right_f32_f32(tmp413, tmp414);
    float tmp416 = left1_4331_43right_f32_f32(tmp409, tmp415);
    float tmp420 = point._m0.z;
    float tmp421 = 2.4300000667572021484375;
    float tmp422 = left1_4321_43right_f32_f32(tmp420, tmp421);
    float tmp423 = left1_4331_43right_f32_f32(tmp416, tmp422);
    float tmp424 = the_sine_of_value_f32(tmp423);
    float tmp425 = 0.5;
    float tmp426 = left1_4321_43right_f32_f32(tmp424, tmp425);
    float tmp427 = 0.5;
    float variation = left1_4331_43right_f32_f32(tmp426, tmp427);
    float tmp428 = 0.75;
    float delay = left1_4321_43right_f32_f32(seed, tmp428);
    float tmp429 = 4.44999980926513671875;
    float tmp430 = left1_4331_43right_f32_f32(tmp429, delay);
    float tmp431 = 6.650000095367431640625;
    float tmp432 = left1_4331_43right_f32_f32(tmp431, delay);
    float assembly = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp430, tmp432, moment);
    float tmp433 = 3.1415927410125732421875;
    float tmp434 = left1_4321_43right_f32_f32(assembly, tmp433);
    float tmp435 = the_sine_of_value_f32(tmp434);
    float tmp436 = 0.100000001490116119384765625;
    float tmp437 = 0.23999999463558197021484375;
    float tmp438 = left1_4321_43right_f32_f32(variation, tmp437);
    float tmp439 = left1_4331_43right_f32_f32(tmp436, tmp438);
    float arc = left1_4321_43right_f32_f32(tmp435, tmp439);
    float tmp440 = 6.283185482025146484375;
    float tmp441 = left1_4321_43right_f32_f32(variation, tmp440);
    float tmp442 = 3.2000000476837158203125;
    float tmp443 = left1_4321_43right_f32_f32(assembly, tmp442);
    float phase = left1_4331_43right_f32_f32(tmp441, tmp443);
    float tmp444 = 3.1415927410125732421875;
    float tmp445 = left1_4321_43right_f32_f32(assembly, tmp444);
    float tmp446 = the_sine_of_value_f32(tmp445);
    float tmp447 = 0.119999997317790985107421875;
    float sweep = left1_4321_43right_f32_f32(tmp446, tmp447);
    float tmp449 = the_cosine_of_value_f32(phase);
    float tmp450 = left1_4321_43right_f32_f32(tmp449, arc);
    float tmp451 = left1_4331_43right_f32_f32(tmp450, sweep);
    vec2 _807 = vec2(0.0);
    _807.x = left1_4321_43right_f32_f32(tmp451, scale);
    float tmp453 = the_sine_of_value_f32(phase);
    float tmp454 = left1_4321_43right_f32_f32(tmp453, arc);
    float tmp455 = 0.7200000286102294921875;
    _807.y = left1_4321_43right_f32_f32(tmp454, tmp455);
    class_0 class_tmp448 = class_0(vec2(0.0));
    class_tmp448._m0 = _807;
    class_0 flight = class_tmp448;
    float tmp463 = motorcycle._m0.x;
    float tmp467 = projection._m0.x;
    float tmp471 = motorcycle._m0.x;
    float tmp472 = left1_4351_43right_f32_f32(tmp467, tmp471);
    float tmp473 = left1_4321_43right_f32_f32(tmp472, assembly);
    float tmp474 = left1_4331_43right_f32_f32(tmp463, tmp473);
    float tmp478 = flight._m0.x;
    vec2 _830 = vec2(0.0);
    _830.x = left1_4331_43right_f32_f32(tmp474, tmp478);
    float tmp483 = motorcycle._m0.y;
    float tmp487 = projection._m0.y;
    float tmp491 = motorcycle._m0.y;
    float tmp492 = left1_4351_43right_f32_f32(tmp487, tmp491);
    float tmp493 = left1_4321_43right_f32_f32(tmp492, assembly);
    float tmp494 = left1_4331_43right_f32_f32(tmp483, tmp493);
    float tmp498 = flight._m0.y;
    _830.y = left1_4331_43right_f32_f32(tmp494, tmp498);
    class_0 class_tmp459 = class_0(vec2(0.0));
    class_tmp459._m0 = _830;
    class_0 assembled = class_tmp459;
    float tmp502 = left1_4351_43right_f32_f32(destination, depth);
    float tmp503 = left1_4321_43right_f32_f32(tmp502, assembly);
    depth = left1_4331_43right_f32_f32(depth, tmp503);
    float tmp504 = 0.0010400000028312206268310546875;
    float tmp505 = 0.959999978542327880859375;
    float tmp506 = 0.039999999105930328369140625;
    float tmp507 = left1_4321_43right_f32_f32(pass, tmp506);
    float tmp508 = left1_4331_43right_f32_f32(tmp505, tmp507);
    float size = left1_4321_43right_f32_f32(tmp504, tmp508);
    vec2 _857 = vec2(0.0);
    _857.x = left1_4321_43right_f32_f32(size, scale);
    _857.y = size;
    class_0 class_tmp509 = class_0(vec2(0.0));
    class_tmp509._m0 = _857;
    class_0 extent = class_tmp509;
    float tmp515 = 0.0;
    float tmp519 = extent._m0.x;
    vec2 _866 = vec2(0.0);
    _866.x = left1_4351_43right_f32_f32(tmp515, tmp519);
    float tmp521 = 0.0;
    float tmp525 = extent._m0.y;
    _866.y = left1_4351_43right_f32_f32(tmp521, tmp525);
    class_0 class_tmp514 = class_0(vec2(0.0));
    class_tmp514._m0 = _866;
    class_0 corner = class_tmp514;
    float tmp531 = 1.0;
    if (left_1is_greater_than_or_equal_to4213_right_f32_f32(triangle, tmp531))
    {
        vec2 _876 = corner._m0;
        _876.x = extent._m0.x;
        corner._m0 = _876;
    }
    float tmp541 = 2.0;
    if (left_1is_greater_than_or_equal_to4213_right_f32_f32(triangle, tmp541))
    {
        vec2 _884 = corner._m0;
        _884.x = 0.0;
        corner._m0 = _884;
        vec2 _888 = corner._m0;
        float tmp551 = extent._m0.y;
        float tmp552 = 1.4500000476837158203125;
        _888.y = left1_4321_43right_f32_f32(tmp551, tmp552);
        corner._m0 = _888;
    }
    float tmp559 = assembled._m0.x;
    float tmp563 = corner._m0.x;
    float tmp564 = left1_4331_43right_f32_f32(tmp559, tmp563);
    vec2 _903 = vec2(0.0);
    _903.x = left1_4321_43right_f32_f32(tmp564, depth);
    float tmp569 = assembled._m0.y;
    float tmp573 = corner._m0.y;
    float tmp574 = left1_4331_43right_f32_f32(tmp569, tmp573);
    _903.y = left1_4321_43right_f32_f32(tmp574, depth);
    class_0 class_tmp555 = class_0(vec2(0.0));
    class_tmp555._m0 = _903;
    class_0 clip = class_tmp555;
    float tmp580 = 0.001000000047497451305389404296875;
    if (left_0_right_f32_f32(visibility, tmp580))
    {
        vec2 _917 = clip._m0;
        float tmp583 = 3.0;
        _917.x = left1_4321_43right_f32_f32(depth, tmp583);
        clip._m0 = _917;
    }
    vec4 _924 = vec4(0.0);
    _924.x = clip._m0.x;
    _924.y = clip._m0.y;
    float tmp594 = 0.4199999868869781494140625;
    _924.z = left1_4321_43right_f32_f32(depth, tmp594);
    _924.w = depth;
    gl_Position = _924;
}
