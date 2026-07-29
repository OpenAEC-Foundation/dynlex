#version 300 es

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

float _the43_floor_of_value_f32(float value)
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

float _the43_maximum_of_a_and_b_f32_f32(float a, float b)
{
    return isnan(b) ? a : (isnan(a) ? b : max(a, b));
}

float _the43_minimum_of_a_and_b_f32_f32(float a, float b)
{
    return isnan(b) ? a : (isnan(a) ? b : min(a, b));
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

float saturate_number_f32(float number)
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

float smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(float lower, float upper, float _sample)
{
    float tmp = left1_4351_43right_f32_f32(_sample, lower);
    float tmp1 = left1_4351_43right_f32_f32(upper, lower);
    float normalized = left1_4371_43right_f32_f32(tmp, tmp1);
    normalized = saturate_number_f32(normalized);
    float tmp2 = left1_4321_43right_f32_f32(normalized, normalized);
    float tmp3 = 3.0;
    float tmp4 = 2.0;
    float tmp5 = left1_4321_43right_f32_f32(tmp4, normalized);
    float tmp6 = left1_4351_43right_f32_f32(tmp3, tmp5);
    return left1_4321_43right_f32_f32(tmp2, tmp6);
}

float scene_window_from_opening_to_closing_at_moment_f32_f32_f32(float opening, float closing, float moment)
{
    float tmp = 0.550000011920928955078125;
    float tmp1 = left1_4331_43right_f32_f32(opening, tmp);
    float arrival = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(opening, tmp1, moment);
    float tmp2 = 1.0;
    float tmp3 = 0.550000011920928955078125;
    float tmp4 = left1_4351_43right_f32_f32(closing, tmp3);
    float tmp5 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp4, closing, moment);
    float departure = left1_4351_43right_f32_f32(tmp2, tmp5);
    return left1_4321_43right_f32_f32(arrival, departure);
}

float _the43_sine_of_value_f32(float value)
{
    return sin(value);
}

float _the43_cosine_of_value_f32(float value)
{
    return cos(value);
}

float _the_431negative_1of_34opposite_1of_3453value_f32(float value)
{
    return -value;
}

bool left_1is_greater_than_or_equal_to4213_right_f32_f32(float left, float right)
{
    return left >= right;
}

void main()
{
    float packed_x = in_Position.x;
    float packed_y = in_Position.y;
    float packed_z = in_Position.z;
    float encoded_triangle_corner = in_Position.w;
    float tmp = 4.0;
    float tmp7 = left1_4371_43right_f32_f32(encoded_triangle_corner, tmp);
    float wheel_point = _the43_floor_of_value_f32(tmp7);
    float tmp8 = 4.0;
    float tmp9 = left1_4321_43right_f32_f32(wheel_point, tmp8);
    float triangle_corner = left1_4351_43right_f32_f32(encoded_triangle_corner, tmp9);
    float tmp10 = 4096.0;
    float tmp11 = left1_4371_43right_f32_f32(packed_x, tmp10);
    float target_quantized_x = _the43_floor_of_value_f32(tmp11);
    float tmp12 = 4096.0;
    float tmp13 = left1_4371_43right_f32_f32(packed_y, tmp12);
    float target_quantized_y = _the43_floor_of_value_f32(tmp13);
    float tmp14 = 4096.0;
    float tmp15 = left1_4371_43right_f32_f32(packed_z, tmp14);
    float target_quantized_z = _the43_floor_of_value_f32(tmp15);
    float tmp16 = 4096.0;
    float tmp17 = left1_4321_43right_f32_f32(target_quantized_x, tmp16);
    float motorcycle_quantized_x = left1_4351_43right_f32_f32(packed_x, tmp17);
    float tmp18 = 4096.0;
    float tmp19 = left1_4321_43right_f32_f32(target_quantized_y, tmp18);
    float motorcycle_quantized_y = left1_4351_43right_f32_f32(packed_y, tmp19);
    float tmp20 = 4096.0;
    float tmp21 = left1_4321_43right_f32_f32(target_quantized_z, tmp20);
    float motorcycle_quantized_z = left1_4351_43right_f32_f32(packed_z, tmp21);
    float tmp22 = 4095.0;
    float tmp23 = left1_4371_43right_f32_f32(target_quantized_x, tmp22);
    float tmp24 = 4.0;
    float tmp25 = left1_4321_43right_f32_f32(tmp23, tmp24);
    float tmp26 = 2.0;
    float point_x = left1_4351_43right_f32_f32(tmp25, tmp26);
    float tmp27 = 4095.0;
    float tmp28 = left1_4371_43right_f32_f32(target_quantized_y, tmp27);
    float tmp29 = 4.0;
    float tmp30 = left1_4321_43right_f32_f32(tmp28, tmp29);
    float tmp31 = 2.0;
    float point_y = left1_4351_43right_f32_f32(tmp30, tmp31);
    float tmp32 = 4095.0;
    float tmp33 = left1_4371_43right_f32_f32(target_quantized_z, tmp32);
    float tmp34 = 4.0;
    float tmp35 = left1_4321_43right_f32_f32(tmp33, tmp34);
    float tmp36 = 2.0;
    float point_z = left1_4351_43right_f32_f32(tmp35, tmp36);
    float tmp37 = 4095.0;
    float tmp38 = left1_4371_43right_f32_f32(motorcycle_quantized_x, tmp37);
    float tmp39 = 4.0;
    float tmp40 = left1_4321_43right_f32_f32(tmp38, tmp39);
    float tmp41 = 2.0;
    float motorcycle_local_x = left1_4351_43right_f32_f32(tmp40, tmp41);
    float tmp42 = 4095.0;
    float tmp43 = left1_4371_43right_f32_f32(motorcycle_quantized_y, tmp42);
    float tmp44 = 4.0;
    float tmp45 = left1_4321_43right_f32_f32(tmp43, tmp44);
    float tmp46 = 2.0;
    float motorcycle_local_y = left1_4351_43right_f32_f32(tmp45, tmp46);
    float tmp47 = 4095.0;
    float tmp48 = left1_4371_43right_f32_f32(motorcycle_quantized_z, tmp47);
    float tmp49 = 4.0;
    float tmp50 = left1_4321_43right_f32_f32(tmp48, tmp49);
    float tmp51 = 2.0;
    float motorcycle_local_z = left1_4351_43right_f32_f32(tmp50, tmp51);
    float time = dynlexUniform0.value;
    float tmp52 = dynlexUniform2.value;
    float tmp53 = 1.0;
    float width = _the43_maximum_of_a_and_b_f32_f32(tmp52, tmp53);
    float tmp54 = dynlexUniform3.value;
    float tmp55 = 1.0;
    float height = _the43_maximum_of_a_and_b_f32_f32(tmp54, tmp55);
    float render_pass = dynlexUniform1.value;
    float aspect = left1_4371_43right_f32_f32(width, height);
    float tmp56 = 1.0;
    float viewport_horizontal_scale = left1_4371_43right_f32_f32(tmp56, aspect);
    float tmp57 = 10.3999996185302734375;
    float moment = _the43_minimum_of_a_and_b_f32_f32(time, tmp57);
    float tmp58 = 0.0;
    float tmp59 = 11.0;
    float visibility = scene_window_from_opening_to_closing_at_moment_f32_f32_f32(tmp58, tmp59, moment);
    float tmp60 = 0.0;
    float tmp61 = left1_4351_43right_f32_f32(tmp60, time);
    float tmp62 = 9.3999996185302734375;
    float motorcycle_wheel_spin = left1_4321_43right_f32_f32(tmp61, tmp62);
    float wheel_spin_sine = _the43_sine_of_value_f32(motorcycle_wheel_spin);
    float wheel_spin_cosine = _the43_cosine_of_value_f32(motorcycle_wheel_spin);
    float tmp63 = 0.7200000286102294921875;
    float wheel_center_x = _the_431negative_1of_34opposite_1of_3453value_f32(tmp63);
    float tmp64 = 0.0;
    if (left_2_right_f32_f32(motorcycle_local_x, tmp64))
    {
        wheel_center_x = 0.7200000286102294921875;
    }
    float wheel_offset_x = left1_4351_43right_f32_f32(motorcycle_local_x, wheel_center_x);
    float tmp65 = 0.4199999868869781494140625;
    float wheel_offset_y = left1_4331_43right_f32_f32(motorcycle_local_y, tmp65);
    float tmp66 = left1_4321_43right_f32_f32(wheel_offset_x, wheel_spin_cosine);
    float tmp67 = left1_4321_43right_f32_f32(wheel_offset_y, wheel_spin_sine);
    float wheel_rotated_x = left1_4351_43right_f32_f32(tmp66, tmp67);
    float tmp68 = left1_4321_43right_f32_f32(wheel_offset_x, wheel_spin_sine);
    float tmp69 = left1_4321_43right_f32_f32(wheel_offset_y, wheel_spin_cosine);
    float wheel_rotated_y = left1_4331_43right_f32_f32(tmp68, tmp69);
    float tmp72 = 0.5;
    if (left_2_right_f32_f32(wheel_point, tmp72))
    {
        motorcycle_local_x = left1_4331_43right_f32_f32(wheel_center_x, wheel_rotated_x);
        float tmp73 = 0.4199999868869781494140625;
        float tmp74 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp73);
        motorcycle_local_y = left1_4331_43right_f32_f32(tmp74, wheel_rotated_y);
    }
    float tmp75 = 0.0;
    float tmp76 = 4.25;
    float motorcycle_progress = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp75, tmp76, moment);
    float tmp77 = 1.2999999523162841796875;
    float tmp78 = 0.039999999105930328369140625;
    float tmp79 = left1_4321_43right_f32_f32(motorcycle_progress, tmp78);
    float motorcycle_yaw = left1_4331_43right_f32_f32(tmp77, tmp79);
    float motorcycle_yaw_sine = _the43_sine_of_value_f32(motorcycle_yaw);
    float motorcycle_yaw_cosine = _the43_cosine_of_value_f32(motorcycle_yaw);
    float tmp80 = left1_4321_43right_f32_f32(motorcycle_local_x, motorcycle_yaw_cosine);
    float tmp81 = left1_4321_43right_f32_f32(motorcycle_local_z, motorcycle_yaw_sine);
    float motorcycle_turned_x = left1_4331_43right_f32_f32(tmp80, tmp81);
    float tmp82 = left1_4321_43right_f32_f32(motorcycle_local_z, motorcycle_yaw_cosine);
    float tmp83 = left1_4321_43right_f32_f32(motorcycle_local_x, motorcycle_yaw_sine);
    float motorcycle_turned_z = left1_4351_43right_f32_f32(tmp82, tmp83);
    float tmp84 = 0.0;
    float tmp85 = 1.2599999904632568359375;
    float tmp86 = left1_4351_43right_f32_f32(tmp84, tmp85);
    float tmp87 = 1.17999994754791259765625;
    float tmp88 = left1_4321_43right_f32_f32(motorcycle_progress, tmp87);
    float tmp89 = left1_4331_43right_f32_f32(tmp86, tmp88);
    float motorcycle_world_x = left1_4331_43right_f32_f32(motorcycle_turned_x, tmp89);
    float tmp90 = 0.0;
    float tmp91 = 0.189999997615814208984375;
    float tmp92 = left1_4351_43right_f32_f32(tmp90, tmp91);
    float tmp93 = 0.119999997317790985107421875;
    float tmp94 = left1_4321_43right_f32_f32(motorcycle_progress, tmp93);
    float tmp95 = left1_4331_43right_f32_f32(tmp92, tmp94);
    float motorcycle_world_y = left1_4331_43right_f32_f32(motorcycle_local_y, tmp95);
    float tmp96 = 2.7999999523162841796875;
    float tmp97 = 4.900000095367431640625;
    float tmp98 = left1_4321_43right_f32_f32(motorcycle_progress, tmp97);
    float tmp99 = left1_4351_43right_f32_f32(tmp96, tmp98);
    float motorcycle_world_z = left1_4331_43right_f32_f32(motorcycle_turned_z, tmp99);
    float tmp100 = 4.19999980926513671875;
    float tmp101 = left1_4331_43right_f32_f32(motorcycle_world_z, tmp100);
    float tmp102 = 0.20000000298023223876953125;
    float motorcycle_depth = _the43_maximum_of_a_and_b_f32_f32(tmp101, tmp102);
    float tmp103 = 1.7200000286102294921875;
    float tmp104 = left1_4321_43right_f32_f32(motorcycle_world_x, tmp103);
    float tmp105 = left1_4321_43right_f32_f32(tmp104, viewport_horizontal_scale);
    float motorcycle_ndc_x = left1_4371_43right_f32_f32(tmp105, motorcycle_depth);
    float tmp106 = 1.7200000286102294921875;
    float tmp107 = left1_4321_43right_f32_f32(motorcycle_world_y, tmp106);
    float motorcycle_ndc_y = left1_4371_43right_f32_f32(tmp107, motorcycle_depth);
    float tmp108 = 0.310000002384185791015625;
    float tmp109 = left1_4321_43right_f32_f32(time, tmp108);
    float tmp110 = _the43_sine_of_value_f32(tmp109);
    float tmp111 = 0.3400000035762786865234375;
    float target_yaw = left1_4321_43right_f32_f32(tmp110, tmp111);
    float target_yaw_sine = _the43_sine_of_value_f32(target_yaw);
    float target_yaw_cosine = _the43_cosine_of_value_f32(target_yaw);
    float tmp112 = left1_4321_43right_f32_f32(point_x, target_yaw_cosine);
    float tmp113 = left1_4321_43right_f32_f32(point_z, target_yaw_sine);
    float target_turned_x = left1_4331_43right_f32_f32(tmp112, tmp113);
    float tmp114 = left1_4321_43right_f32_f32(point_z, target_yaw_cosine);
    float tmp115 = left1_4321_43right_f32_f32(point_x, target_yaw_sine);
    float target_turned_z = left1_4351_43right_f32_f32(tmp114, tmp115);
    float tmp116 = 3.25;
    float tmp117 = 0.519999980926513671875;
    float tmp118 = left1_4321_43right_f32_f32(target_turned_z, tmp117);
    float target_depth = left1_4331_43right_f32_f32(tmp116, tmp118);
    float tmp119 = 1.65999996662139892578125;
    float tmp120 = left1_4321_43right_f32_f32(target_turned_x, tmp119);
    float tmp121 = left1_4321_43right_f32_f32(tmp120, viewport_horizontal_scale);
    float target_ndc_x = left1_4371_43right_f32_f32(tmp121, target_depth);
    float tmp122 = 1.86000001430511474609375;
    float tmp123 = left1_4321_43right_f32_f32(point_y, tmp122);
    float tmp124 = 0.039999999105930328369140625;
    float tmp125 = left1_4351_43right_f32_f32(tmp123, tmp124);
    float target_ndc_y = left1_4371_43right_f32_f32(tmp125, target_depth);
    float tmp126 = 3.13000011444091796875;
    float tmp127 = left1_4321_43right_f32_f32(point_x, tmp126);
    float tmp128 = 2.71000003814697265625;
    float tmp129 = left1_4321_43right_f32_f32(point_y, tmp128);
    float tmp130 = left1_4331_43right_f32_f32(tmp127, tmp129);
    float tmp131 = 4.190000057220458984375;
    float tmp132 = left1_4321_43right_f32_f32(point_z, tmp131);
    float tmp133 = left1_4331_43right_f32_f32(tmp130, tmp132);
    float tmp134 = _the43_sine_of_value_f32(tmp133);
    float tmp135 = 0.5;
    float tmp136 = left1_4321_43right_f32_f32(tmp134, tmp135);
    float tmp137 = 0.5;
    float drone_seed_a = left1_4331_43right_f32_f32(tmp136, tmp137);
    float tmp138 = 2.1700000762939453125;
    float tmp139 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp138);
    float tmp140 = left1_4321_43right_f32_f32(point_x, tmp139);
    float tmp141 = 3.9100000858306884765625;
    float tmp142 = left1_4321_43right_f32_f32(point_y, tmp141);
    float tmp143 = left1_4331_43right_f32_f32(tmp140, tmp142);
    float tmp144 = 2.4300000667572021484375;
    float tmp145 = left1_4321_43right_f32_f32(point_z, tmp144);
    float tmp146 = left1_4331_43right_f32_f32(tmp143, tmp145);
    float tmp147 = _the43_sine_of_value_f32(tmp146);
    float tmp148 = 0.5;
    float tmp149 = left1_4321_43right_f32_f32(tmp147, tmp148);
    float tmp150 = 0.5;
    float drone_seed_b = left1_4331_43right_f32_f32(tmp149, tmp150);
    float tmp151 = 0.75;
    float drone_delay = left1_4321_43right_f32_f32(drone_seed_a, tmp151);
    float tmp152 = 4.44999980926513671875;
    float tmp153 = left1_4331_43right_f32_f32(tmp152, drone_delay);
    float tmp154 = 6.650000095367431640625;
    float tmp155 = left1_4331_43right_f32_f32(tmp154, drone_delay);
    float assembly_progress = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp153, tmp155, moment);
    float tmp156 = 3.1415927410125732421875;
    float tmp157 = left1_4321_43right_f32_f32(assembly_progress, tmp156);
    float tmp158 = _the43_sine_of_value_f32(tmp157);
    float tmp159 = 0.100000001490116119384765625;
    float tmp160 = 0.23999999463558197021484375;
    float tmp161 = left1_4321_43right_f32_f32(drone_seed_b, tmp160);
    float tmp162 = left1_4331_43right_f32_f32(tmp159, tmp161);
    float flight_arc = left1_4321_43right_f32_f32(tmp158, tmp162);
    float tmp163 = 6.283185482025146484375;
    float tmp164 = left1_4321_43right_f32_f32(drone_seed_b, tmp163);
    float tmp165 = 3.2000000476837158203125;
    float tmp166 = left1_4321_43right_f32_f32(assembly_progress, tmp165);
    float flight_phase = left1_4331_43right_f32_f32(tmp164, tmp166);
    float tmp167 = 3.1415927410125732421875;
    float tmp168 = left1_4321_43right_f32_f32(assembly_progress, tmp167);
    float tmp169 = _the43_sine_of_value_f32(tmp168);
    float tmp170 = 0.119999997317790985107421875;
    float flight_sweep = left1_4321_43right_f32_f32(tmp169, tmp170);
    float tmp171 = _the43_cosine_of_value_f32(flight_phase);
    float tmp172 = left1_4321_43right_f32_f32(tmp171, flight_arc);
    float tmp173 = left1_4331_43right_f32_f32(tmp172, flight_sweep);
    float flight_x = left1_4321_43right_f32_f32(tmp173, viewport_horizontal_scale);
    float tmp174 = _the43_sine_of_value_f32(flight_phase);
    float tmp175 = left1_4321_43right_f32_f32(tmp174, flight_arc);
    float tmp176 = 0.7200000286102294921875;
    float flight_y = left1_4321_43right_f32_f32(tmp175, tmp176);
    float tmp177 = left1_4351_43right_f32_f32(target_ndc_x, motorcycle_ndc_x);
    float tmp178 = left1_4321_43right_f32_f32(tmp177, assembly_progress);
    float tmp179 = left1_4331_43right_f32_f32(motorcycle_ndc_x, tmp178);
    float assembled_ndc_x = left1_4331_43right_f32_f32(tmp179, flight_x);
    float tmp180 = left1_4351_43right_f32_f32(target_ndc_y, motorcycle_ndc_y);
    float tmp181 = left1_4321_43right_f32_f32(tmp180, assembly_progress);
    float tmp182 = left1_4331_43right_f32_f32(motorcycle_ndc_y, tmp181);
    float assembled_ndc_y = left1_4331_43right_f32_f32(tmp182, flight_y);
    float tmp183 = left1_4351_43right_f32_f32(target_depth, motorcycle_depth);
    float tmp184 = left1_4321_43right_f32_f32(tmp183, assembly_progress);
    float depth = left1_4331_43right_f32_f32(motorcycle_depth, tmp184);
    float tmp185 = 0.0010400000028312206268310546875;
    float tmp186 = 0.959999978542327880859375;
    float tmp187 = 0.039999999105930328369140625;
    float tmp188 = left1_4321_43right_f32_f32(render_pass, tmp187);
    float tmp189 = left1_4331_43right_f32_f32(tmp186, tmp188);
    float relative_point_size = left1_4321_43right_f32_f32(tmp185, tmp189);
    float point_size_x = left1_4321_43right_f32_f32(relative_point_size, viewport_horizontal_scale);
    float point_size_y = relative_point_size;
    float tmp190 = 0.0;
    float corner_x = left1_4351_43right_f32_f32(tmp190, point_size_x);
    float tmp191 = 0.0;
    float corner_y = left1_4351_43right_f32_f32(tmp191, point_size_y);
    float tmp194 = 1.0;
    if (left_1is_greater_than_or_equal_to4213_right_f32_f32(triangle_corner, tmp194))
    {
        corner_x = point_size_x;
    }
    float tmp197 = 2.0;
    if (left_1is_greater_than_or_equal_to4213_right_f32_f32(triangle_corner, tmp197))
    {
        corner_x = 0.0;
        float tmp198 = 1.4500000476837158203125;
        corner_y = left1_4321_43right_f32_f32(point_size_y, tmp198);
    }
    float tmp199 = left1_4331_43right_f32_f32(assembled_ndc_x, corner_x);
    float tmp200 = left1_4331_43right_f32_f32(assembled_ndc_y, corner_y);
    float tmp203 = 0.001000000047497451305389404296875;
    float _552 = 0.0;
    if (left_0_right_f32_f32(visibility, tmp203))
    {
        float tmp204 = 3.0;
        _552 = left1_4321_43right_f32_f32(depth, tmp204);
    }
    else
    {
        _552 = left1_4321_43right_f32_f32(tmp199, depth);
    }
    float tmp205 = 0.4199999868869781494140625;
    vec4 _555 = vec4(0.0);
    _555.w = depth;
    _555.z = left1_4321_43right_f32_f32(depth, tmp205);
    _555.y = left1_4321_43right_f32_f32(tmp200, depth);
    _555.x = _552;
    gl_Position = _555;
}
