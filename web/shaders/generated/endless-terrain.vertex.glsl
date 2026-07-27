#version 300 es

layout(std140) uniform DynlexUniformBlock0
{
    float value;
} dynlexUniform0;

layout(std140) uniform DynlexUniformBlock1
{
    float value;
} dynlexUniform1;

layout(std140) uniform DynlexUniformBlock2
{
    float value;
} dynlexUniform2;

layout(location = 0) in vec4 in_Position;
out vec4 dynlex_interpolant_7465727261696e5f706f736974696f6e;
out vec4 dynlex_interpolant_7465727261696e5f6e6f726d616c;
out vec4 dynlex_interpolant_7465727261696e5f6d6174657269616c;

float _the43_maximum_of_a_and_b_f32_f32(float a, float b)
{
    return isnan(b) ? a : (isnan(a) ? b : max(a, b));
}

float left1_4371_43right_f32_f32(float left, float right)
{
    return left / right;
}

bool left_0_right_f32_f32(float left, float right)
{
    return left < right;
}

float left1_4321_43right_f32_f32(float left, float right)
{
    return left * right;
}

float left1_4331_43right_f32_f32(float left, float right)
{
    return left + right;
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

float left1_4351_43right_f32_f32(float left, float right)
{
    return left - right;
}

float _the43_floor_of_value_f32(float value)
{
    return floor(value);
}

bool left_2_right_f32_f32(float left, float right)
{
    return left > right;
}

float simplex_permutation_of_value_f32(float value)
{
    float tmp = 34.0;
    float tmp1 = left1_4321_43right_f32_f32(value, tmp);
    float tmp2 = 1.0;
    float tmp3 = left1_4331_43right_f32_f32(tmp1, tmp2);
    float polynomial = left1_4321_43right_f32_f32(tmp3, value);
    float tmp4 = 289.0;
    float tmp5 = left1_4371_43right_f32_f32(polynomial, tmp4);
    float tmp6 = _the43_floor_of_value_f32(tmp5);
    float tmp7 = 289.0;
    float tmp8 = left1_4321_43right_f32_f32(tmp6, tmp7);
    return left1_4351_43right_f32_f32(polynomial, tmp8);
}

float fractional_part_of_number_f32(float number)
{
    float tmp = _the43_floor_of_value_f32(number);
    return left1_4351_43right_f32_f32(number, tmp);
}

float _the43_absolute_value_of_magnitude_f32(float magnitude)
{
    return abs(magnitude);
}

float simplex_corner_at_x_y_permutation_f32_f32_f32(float x, float y, float permutation)
{
    float tmp = 0.5;
    float tmp1 = left1_4321_43right_f32_f32(x, x);
    float tmp2 = left1_4321_43right_f32_f32(y, y);
    float tmp3 = left1_4331_43right_f32_f32(tmp1, tmp2);
    float tmp4 = left1_4351_43right_f32_f32(tmp, tmp3);
    float tmp5 = 0.0;
    float radius = _the43_maximum_of_a_and_b_f32_f32(tmp4, tmp5);
    float tmp6 = 41.0;
    float tmp7 = left1_4371_43right_f32_f32(permutation, tmp6);
    float tmp8 = fractional_part_of_number_f32(tmp7);
    float tmp9 = 2.0;
    float tmp10 = left1_4321_43right_f32_f32(tmp8, tmp9);
    float tmp11 = 1.0;
    float gradient_axis = left1_4351_43right_f32_f32(tmp10, tmp11);
    float tmp12 = _the43_absolute_value_of_magnitude_f32(gradient_axis);
    float tmp13 = 0.5;
    float gradient_height = left1_4351_43right_f32_f32(tmp12, tmp13);
    float tmp14 = 0.5;
    float tmp15 = left1_4331_43right_f32_f32(gradient_axis, tmp14);
    float gradient_offset = _the43_floor_of_value_f32(tmp15);
    gradient_axis = left1_4351_43right_f32_f32(gradient_axis, gradient_offset);
    float tmp16 = 1.792842864990234375;
    float tmp17 = 0.8537347316741943359375;
    float tmp18 = left1_4321_43right_f32_f32(gradient_axis, gradient_axis);
    float tmp19 = left1_4321_43right_f32_f32(gradient_height, gradient_height);
    float tmp20 = left1_4331_43right_f32_f32(tmp18, tmp19);
    float tmp21 = left1_4321_43right_f32_f32(tmp17, tmp20);
    float normalization = left1_4351_43right_f32_f32(tmp16, tmp21);
    float attenuation = left1_4321_43right_f32_f32(radius, radius);
    attenuation = left1_4321_43right_f32_f32(attenuation, attenuation);
    float tmp22 = left1_4321_43right_f32_f32(attenuation, normalization);
    float tmp23 = left1_4321_43right_f32_f32(gradient_axis, x);
    float tmp24 = left1_4321_43right_f32_f32(gradient_height, y);
    float tmp25 = left1_4331_43right_f32_f32(tmp23, tmp24);
    return left1_4321_43right_f32_f32(tmp22, tmp25);
}

float simplex_field_at_x_y_phase_f32_f32_f32(float x, float y, float phase)
{
    float tmp = 17.1700000762939453125;
    float tmp1 = left1_4321_43right_f32_f32(phase, tmp);
    float sample_x = left1_4331_43right_f32_f32(x, tmp1);
    float tmp2 = 11.13000011444091796875;
    float tmp3 = left1_4321_43right_f32_f32(phase, tmp2);
    float sample_y = left1_4351_43right_f32_f32(y, tmp3);
    float tmp4 = left1_4331_43right_f32_f32(sample_x, sample_y);
    float tmp5 = 0.366025388240814208984375;
    float skew = left1_4321_43right_f32_f32(tmp4, tmp5);
    float tmp6 = left1_4331_43right_f32_f32(sample_x, skew);
    float corner_x = _the43_floor_of_value_f32(tmp6);
    float tmp7 = left1_4331_43right_f32_f32(sample_y, skew);
    float corner_y = _the43_floor_of_value_f32(tmp7);
    float tmp8 = left1_4331_43right_f32_f32(corner_x, corner_y);
    float tmp9 = 0.211324870586395263671875;
    float unskew = left1_4321_43right_f32_f32(tmp8, tmp9);
    float tmp10 = left1_4351_43right_f32_f32(corner_x, unskew);
    float local_x = left1_4351_43right_f32_f32(sample_x, tmp10);
    float tmp11 = left1_4351_43right_f32_f32(corner_y, unskew);
    float local_y = left1_4351_43right_f32_f32(sample_y, tmp11);
    float second_x = 0.0;
    if (left_2_right_f32_f32(local_x, local_y))
    {
        second_x = 1.0;
    }
    float tmp12 = 1.0;
    float second_y = left1_4351_43right_f32_f32(tmp12, second_x);
    float tmp13 = left1_4351_43right_f32_f32(local_x, second_x);
    float tmp14 = 0.211324870586395263671875;
    float second_local_x = left1_4331_43right_f32_f32(tmp13, tmp14);
    float tmp15 = left1_4351_43right_f32_f32(local_y, second_y);
    float tmp16 = 0.211324870586395263671875;
    float second_local_y = left1_4331_43right_f32_f32(tmp15, tmp16);
    float tmp17 = 0.57735025882720947265625;
    float third_local_x = left1_4351_43right_f32_f32(local_x, tmp17);
    float tmp18 = 0.57735025882720947265625;
    float third_local_y = left1_4351_43right_f32_f32(local_y, tmp18);
    float tmp19 = 289.0;
    float tmp20 = left1_4371_43right_f32_f32(corner_x, tmp19);
    float tmp21 = _the43_floor_of_value_f32(tmp20);
    float tmp22 = 289.0;
    float tmp23 = left1_4321_43right_f32_f32(tmp21, tmp22);
    float wrapped_x = left1_4351_43right_f32_f32(corner_x, tmp23);
    float tmp24 = 289.0;
    float tmp25 = left1_4371_43right_f32_f32(corner_y, tmp24);
    float tmp26 = _the43_floor_of_value_f32(tmp25);
    float tmp27 = 289.0;
    float tmp28 = left1_4321_43right_f32_f32(tmp26, tmp27);
    float wrapped_y = left1_4351_43right_f32_f32(corner_y, tmp28);
    float tmp29 = simplex_permutation_of_value_f32(wrapped_y);
    float tmp30 = left1_4331_43right_f32_f32(tmp29, wrapped_x);
    float first_permutation = simplex_permutation_of_value_f32(tmp30);
    float tmp31 = left1_4331_43right_f32_f32(wrapped_y, second_y);
    float tmp32 = simplex_permutation_of_value_f32(tmp31);
    float tmp33 = left1_4331_43right_f32_f32(tmp32, wrapped_x);
    float tmp34 = left1_4331_43right_f32_f32(tmp33, second_x);
    float second_permutation = simplex_permutation_of_value_f32(tmp34);
    float tmp35 = 1.0;
    float tmp36 = left1_4331_43right_f32_f32(wrapped_y, tmp35);
    float tmp37 = simplex_permutation_of_value_f32(tmp36);
    float tmp38 = left1_4331_43right_f32_f32(tmp37, wrapped_x);
    float tmp39 = 1.0;
    float tmp40 = left1_4331_43right_f32_f32(tmp38, tmp39);
    float third_permutation = simplex_permutation_of_value_f32(tmp40);
    float first_corner = simplex_corner_at_x_y_permutation_f32_f32_f32(local_x, local_y, first_permutation);
    float second_corner = simplex_corner_at_x_y_permutation_f32_f32_f32(second_local_x, second_local_y, second_permutation);
    float third_corner = simplex_corner_at_x_y_permutation_f32_f32_f32(third_local_x, third_local_y, third_permutation);
    float tmp41 = left1_4331_43right_f32_f32(first_corner, second_corner);
    float tmp42 = left1_4331_43right_f32_f32(tmp41, third_corner);
    float tmp43 = 130.0;
    return left1_4321_43right_f32_f32(tmp42, tmp43);
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

float terrain_height_at_x_z_f32_f32(float x, float z)
{
    float tmp = 0.010999999940395355224609375;
    float tmp1 = left1_4321_43right_f32_f32(x, tmp);
    float tmp2 = 0.010999999940395355224609375;
    float tmp3 = left1_4321_43right_f32_f32(z, tmp2);
    float tmp4 = 1.7000000476837158203125;
    float continental_fold = simplex_field_at_x_y_phase_f32_f32_f32(tmp1, tmp3, tmp4);
    float tmp5 = 0.02099999971687793731689453125;
    float tmp6 = left1_4321_43right_f32_f32(x, tmp5);
    float tmp7 = 9.0;
    float tmp8 = left1_4331_43right_f32_f32(tmp6, tmp7);
    float tmp9 = 0.02099999971687793731689453125;
    float tmp10 = left1_4321_43right_f32_f32(z, tmp9);
    float tmp11 = 4.0;
    float tmp12 = left1_4351_43right_f32_f32(tmp10, tmp11);
    float tmp13 = 4.099999904632568359375;
    float warp_x = simplex_field_at_x_y_phase_f32_f32_f32(tmp8, tmp12, tmp13);
    float tmp14 = 0.006000000052154064178466796875;
    float tmp15 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp14);
    float tmp16 = left1_4321_43right_f32_f32(x, tmp15);
    float tmp17 = 0.01899999938905239105224609375;
    float tmp18 = left1_4321_43right_f32_f32(z, tmp17);
    float tmp19 = left1_4331_43right_f32_f32(tmp16, tmp18);
    float tmp20 = 0.007000000216066837310791015625;
    float tmp21 = left1_4321_43right_f32_f32(z, tmp20);
    float tmp22 = 0.0199999995529651641845703125;
    float tmp23 = left1_4321_43right_f32_f32(x, tmp22);
    float tmp24 = left1_4331_43right_f32_f32(tmp21, tmp23);
    float tmp25 = 8.30000019073486328125;
    float warp_z = simplex_field_at_x_y_phase_f32_f32_f32(tmp19, tmp24, tmp25);
    float tmp26 = 9.5;
    float tmp27 = left1_4321_43right_f32_f32(warp_x, tmp26);
    float bent_x = left1_4331_43right_f32_f32(x, tmp27);
    float tmp28 = 9.5;
    float tmp29 = left1_4321_43right_f32_f32(warp_z, tmp28);
    float bent_z = left1_4331_43right_f32_f32(z, tmp29);
    float tmp30 = 0.02700000070035457611083984375;
    float tmp31 = left1_4321_43right_f32_f32(bent_x, tmp30);
    float tmp32 = 0.006000000052154064178466796875;
    float tmp33 = left1_4321_43right_f32_f32(bent_z, tmp32);
    float tmp34 = left1_4331_43right_f32_f32(tmp31, tmp33);
    float tmp35 = 0.02700000070035457611083984375;
    float tmp36 = left1_4321_43right_f32_f32(bent_z, tmp35);
    float tmp37 = 0.006000000052154064178466796875;
    float tmp38 = left1_4321_43right_f32_f32(bent_x, tmp37);
    float tmp39 = left1_4351_43right_f32_f32(tmp36, tmp38);
    float tmp40 = 2.2999999523162841796875;
    float mountain_wave = simplex_field_at_x_y_phase_f32_f32_f32(tmp34, tmp39, tmp40);
    float tmp41 = 0.017999999225139617919921875;
    float tmp42 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp41);
    float tmp43 = left1_4321_43right_f32_f32(bent_x, tmp42);
    float tmp44 = 0.0570000000298023223876953125;
    float tmp45 = left1_4321_43right_f32_f32(bent_z, tmp44);
    float tmp46 = left1_4331_43right_f32_f32(tmp43, tmp45);
    float tmp47 = 0.01899999938905239105224609375;
    float tmp48 = left1_4321_43right_f32_f32(bent_z, tmp47);
    float tmp49 = 0.0540000014007091522216796875;
    float tmp50 = left1_4321_43right_f32_f32(bent_x, tmp49);
    float tmp51 = left1_4331_43right_f32_f32(tmp48, tmp50);
    float tmp52 = 6.900000095367431640625;
    float crossing_wave = simplex_field_at_x_y_phase_f32_f32_f32(tmp46, tmp51, tmp52);
    float tmp53 = 0.11900000274181365966796875;
    float tmp54 = left1_4321_43right_f32_f32(bent_x, tmp53);
    float tmp55 = 0.041000001132488250732421875;
    float tmp56 = left1_4321_43right_f32_f32(bent_z, tmp55);
    float tmp57 = left1_4331_43right_f32_f32(tmp54, tmp56);
    float tmp58 = 0.112999998033046722412109375;
    float tmp59 = left1_4321_43right_f32_f32(bent_z, tmp58);
    float tmp60 = 0.037000000476837158203125;
    float tmp61 = left1_4321_43right_f32_f32(bent_x, tmp60);
    float tmp62 = left1_4351_43right_f32_f32(tmp59, tmp61);
    float tmp63 = 9.69999980926513671875;
    float broken_wave = simplex_field_at_x_y_phase_f32_f32_f32(tmp57, tmp62, tmp63);
    float tmp64 = 0.60000002384185791015625;
    float tmp65 = left1_4321_43right_f32_f32(mountain_wave, tmp64);
    float tmp66 = 0.2700000107288360595703125;
    float tmp67 = left1_4321_43right_f32_f32(crossing_wave, tmp66);
    float tmp68 = left1_4331_43right_f32_f32(tmp65, tmp67);
    float tmp69 = 0.12999999523162841796875;
    float tmp70 = left1_4321_43right_f32_f32(broken_wave, tmp69);
    float folded_mass = left1_4331_43right_f32_f32(tmp68, tmp70);
    float tmp71 = 0.0;
    float mountain_ridge = _the43_maximum_of_a_and_b_f32_f32(folded_mass, tmp71);
    mountain_ridge = left1_4321_43right_f32_f32(mountain_ridge, mountain_ridge);
    float tmp72 = 1.0;
    float tmp73 = 0.7799999713897705078125;
    float tmp74 = left1_4321_43right_f32_f32(mountain_wave, tmp73);
    float tmp75 = 0.3400000035762786865234375;
    float tmp76 = left1_4321_43right_f32_f32(crossing_wave, tmp75);
    float tmp77 = left1_4331_43right_f32_f32(tmp74, tmp76);
    float tmp78 = _the43_absolute_value_of_magnitude_f32(tmp77);
    float ridge_fold = left1_4351_43right_f32_f32(tmp72, tmp78);
    float tmp79 = 0.4199999868869781494140625;
    float tmp80 = left1_4351_43right_f32_f32(ridge_fold, tmp79);
    float tmp81 = 0.0;
    float ridge_crest = _the43_maximum_of_a_and_b_f32_f32(tmp80, tmp81);
    ridge_crest = left1_4321_43right_f32_f32(ridge_crest, ridge_crest);
    float tmp82 = 0.4799999892711639404296875;
    float tmp83 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp82);
    float tmp84 = 0.449999988079071044921875;
    float uplift = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp83, tmp84, continental_fold);
    float tmp85 = 1.0;
    float tmp86 = 0.0350000001490116119384765625;
    float tmp87 = 0.310000002384185791015625;
    float tmp88 = 0.23999999463558197021484375;
    float tmp89 = left1_4321_43right_f32_f32(broken_wave, tmp88);
    float tmp90 = left1_4331_43right_f32_f32(crossing_wave, tmp89);
    float tmp91 = _the43_absolute_value_of_magnitude_f32(tmp90);
    float tmp92 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp86, tmp87, tmp91);
    float erosion_channels = left1_4351_43right_f32_f32(tmp85, tmp92);
    float tmp93 = 1.37999999523162841796875;
    float alpine_mass = left1_4321_43right_f32_f32(folded_mass, tmp93);
    float tmp94 = 2.0;
    float tmp95 = 6.0;
    float tmp96 = left1_4321_43right_f32_f32(uplift, tmp95);
    float tmp97 = left1_4331_43right_f32_f32(tmp94, tmp96);
    float tmp98 = left1_4321_43right_f32_f32(mountain_ridge, tmp97);
    alpine_mass = left1_4331_43right_f32_f32(alpine_mass, tmp98);
    float tmp99 = left1_4321_43right_f32_f32(ridge_crest, uplift);
    float tmp100 = 3.099999904632568359375;
    float tmp101 = 2.400000095367431640625;
    float tmp102 = left1_4321_43right_f32_f32(mountain_ridge, tmp101);
    float tmp103 = left1_4331_43right_f32_f32(tmp100, tmp102);
    float tmp104 = left1_4321_43right_f32_f32(tmp99, tmp103);
    alpine_mass = left1_4331_43right_f32_f32(alpine_mass, tmp104);
    float tmp105 = left1_4321_43right_f32_f32(broken_wave, uplift);
    float tmp106 = 0.3400000035762786865234375;
    float tmp107 = left1_4321_43right_f32_f32(tmp105, tmp106);
    alpine_mass = left1_4331_43right_f32_f32(alpine_mass, tmp107);
    float tmp108 = left1_4321_43right_f32_f32(erosion_channels, uplift);
    float tmp109 = 0.1599999964237213134765625;
    float tmp110 = 0.37999999523162841796875;
    float tmp111 = left1_4321_43right_f32_f32(mountain_ridge, tmp110);
    float tmp112 = left1_4331_43right_f32_f32(tmp109, tmp111);
    float tmp113 = left1_4321_43right_f32_f32(tmp108, tmp112);
    alpine_mass = left1_4351_43right_f32_f32(alpine_mass, tmp113);
    float tmp114 = 1.2000000476837158203125;
    float basin = left1_4321_43right_f32_f32(continental_fold, tmp114);
    float tmp115 = left1_4331_43right_f32_f32(basin, alpine_mass);
    float tmp116 = 0.75;
    return left1_4351_43right_f32_f32(tmp115, tmp116);
}

float _the43_square_root_of_value_f32(float value)
{
    return sqrt(value);
}

void main()
{
    float time = dynlexUniform0.value;
    float tmp = dynlexUniform1.value;
    float tmp1 = 1.0;
    float width = _the43_maximum_of_a_and_b_f32_f32(tmp, tmp1);
    float tmp2 = dynlexUniform2.value;
    float tmp3 = 1.0;
    float height = _the43_maximum_of_a_and_b_f32_f32(tmp2, tmp3);
    float aspect = left1_4371_43right_f32_f32(width, height);
    float surface_vertex = in_Position.w;
    float tmp4 = 0.5;
    if (left_0_right_f32_f32(surface_vertex, tmp4))
    {
        vec4 _408 = vec4(0.0);
        _408.y = in_Position.y;
        _408.x = in_Position.x;
        dynlex_interpolant_7465727261696e5f706f736974696f6e = _408;
        dynlex_interpolant_7465727261696e5f6e6f726d616c = vec4(0.0, 1.0, 0.0, 0.0);
        dynlex_interpolant_7465727261696e5f6d6174657269616c = vec4(0.5, 0.0, 0.0, 0.0);
        vec4 _414 = vec4(0.0, 0.0, 0.99989998340606689453125, 1.0);
        _414.y = in_Position.y;
        _414.x = in_Position.x;
        gl_Position = _414;
    }
    else
    {
        float depth_fraction = in_Position.y;
        float tmp17 = 0.449999988079071044921875;
        float tmp18 = left1_4321_43right_f32_f32(depth_fraction, depth_fraction);
        float tmp19 = 94.0;
        float tmp20 = left1_4321_43right_f32_f32(tmp18, tmp19);
        float view_distance = left1_4331_43right_f32_f32(tmp17, tmp20);
        float tmp21 = 201.6999969482421875;
        float tmp22 = 0.090999998152256011962890625;
        float tmp23 = left1_4321_43right_f32_f32(time, tmp22);
        float tmp24 = _the43_sine_of_value_f32(tmp23);
        float tmp25 = 4.19999980926513671875;
        float tmp26 = left1_4321_43right_f32_f32(tmp24, tmp25);
        float tmp27 = 0.037000000476837158203125;
        float tmp28 = left1_4321_43right_f32_f32(time, tmp27);
        float tmp29 = _the43_cosine_of_value_f32(tmp28);
        float tmp30 = 1.60000002384185791015625;
        float tmp31 = left1_4321_43right_f32_f32(tmp29, tmp30);
        float tmp32 = left1_4331_43right_f32_f32(tmp26, tmp31);
        float camera_x = left1_4331_43right_f32_f32(tmp21, tmp32);
        float tmp33 = 92.09999847412109375;
        float tmp34 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp33);
        float tmp35 = 3.25;
        float tmp36 = left1_4321_43right_f32_f32(time, tmp35);
        float camera_z = left1_4331_43right_f32_f32(tmp34, tmp36);
        float camera_ground = terrain_height_at_x_z_f32_f32(camera_x, camera_z);
        float tmp37 = 3.150000095367431640625;
        float tmp38 = left1_4331_43right_f32_f32(camera_ground, tmp37);
        float tmp39 = 0.17000000178813934326171875;
        float tmp40 = left1_4321_43right_f32_f32(time, tmp39);
        float tmp41 = _the43_sine_of_value_f32(tmp40);
        float tmp42 = 0.14000000059604644775390625;
        float tmp43 = left1_4321_43right_f32_f32(tmp41, tmp42);
        float camera_y = left1_4331_43right_f32_f32(tmp38, tmp43);
        float tmp44 = 0.07299999892711639404296875;
        float tmp45 = left1_4321_43right_f32_f32(time, tmp44);
        float tmp46 = _the43_sine_of_value_f32(tmp45);
        float tmp47 = 0.12999999523162841796875;
        float tmp48 = left1_4321_43right_f32_f32(tmp46, tmp47);
        float tmp49 = 0.041000001132488250732421875;
        float tmp50 = left1_4321_43right_f32_f32(time, tmp49);
        float tmp51 = _the43_cosine_of_value_f32(tmp50);
        float tmp52 = 0.04500000178813934326171875;
        float tmp53 = left1_4321_43right_f32_f32(tmp51, tmp52);
        float yaw = left1_4331_43right_f32_f32(tmp48, tmp53);
        float yaw_sine = _the43_sine_of_value_f32(yaw);
        float yaw_cosine = _the43_cosine_of_value_f32(yaw);
        float tmp54 = 1.0;
        float tmp55 = left1_4351_43right_f32_f32(tmp54, depth_fraction);
        float tmp56 = 1.0;
        float tmp57 = left1_4351_43right_f32_f32(tmp56, depth_fraction);
        float tmp58 = left1_4321_43right_f32_f32(tmp55, tmp57);
        float tmp59 = 4.80000019073486328125;
        float near_spread = left1_4321_43right_f32_f32(tmp58, tmp59);
        float tmp62 = in_Position.x;
        float tmp63 = left1_4331_43right_f32_f32(view_distance, near_spread);
        float tmp64 = left1_4321_43right_f32_f32(tmp62, tmp63);
        float tmp65 = left1_4321_43right_f32_f32(tmp64, aspect);
        float tmp66 = 0.800000011920928955078125;
        float lateral_distance = left1_4321_43right_f32_f32(tmp65, tmp66);
        float tmp67 = left1_4321_43right_f32_f32(lateral_distance, yaw_cosine);
        float tmp68 = left1_4331_43right_f32_f32(camera_x, tmp67);
        float tmp69 = left1_4321_43right_f32_f32(view_distance, yaw_sine);
        float world_x = left1_4331_43right_f32_f32(tmp68, tmp69);
        float tmp70 = left1_4321_43right_f32_f32(view_distance, yaw_cosine);
        float tmp71 = left1_4331_43right_f32_f32(camera_z, tmp70);
        float tmp72 = left1_4321_43right_f32_f32(lateral_distance, yaw_sine);
        float world_z = left1_4351_43right_f32_f32(tmp71, tmp72);
        float displaced_height = terrain_height_at_x_z_f32_f32(world_x, world_z);
        float tmp73 = 0.0900000035762786865234375;
        float tmp74 = 0.014999999664723873138427734375;
        float tmp75 = left1_4321_43right_f32_f32(view_distance, tmp74);
        float normal_step = left1_4331_43right_f32_f32(tmp73, tmp75);
        float tmp76 = left1_4331_43right_f32_f32(world_x, normal_step);
        float height_right = terrain_height_at_x_z_f32_f32(tmp76, world_z);
        float tmp77 = left1_4331_43right_f32_f32(world_z, normal_step);
        float height_front = terrain_height_at_x_z_f32_f32(world_x, tmp77);
        float normal_x = left1_4351_43right_f32_f32(displaced_height, height_right);
        float normal_y = normal_step;
        float normal_z = left1_4351_43right_f32_f32(displaced_height, height_front);
        float tmp78 = left1_4321_43right_f32_f32(normal_x, normal_x);
        float tmp79 = left1_4321_43right_f32_f32(normal_y, normal_y);
        float tmp80 = left1_4331_43right_f32_f32(tmp78, tmp79);
        float tmp81 = left1_4321_43right_f32_f32(normal_z, normal_z);
        float tmp82 = left1_4331_43right_f32_f32(tmp80, tmp81);
        float normal_length = _the43_square_root_of_value_f32(tmp82);
        normal_x = left1_4371_43right_f32_f32(normal_x, normal_length);
        normal_y = left1_4371_43right_f32_f32(normal_y, normal_length);
        normal_z = left1_4371_43right_f32_f32(normal_z, normal_length);
        float tmp83 = 0.1550000011920928955078125;
        float tmp84 = 0.10999999940395355224609375;
        float tmp85 = left1_4321_43right_f32_f32(time, tmp84);
        float tmp86 = _the43_sine_of_value_f32(tmp85);
        float tmp87 = 0.01200000010430812835693359375;
        float tmp88 = left1_4321_43right_f32_f32(tmp86, tmp87);
        float camera_pitch = left1_4331_43right_f32_f32(tmp83, tmp88);
        float pitch_sine = _the43_sine_of_value_f32(camera_pitch);
        float pitch_cosine = _the43_cosine_of_value_f32(camera_pitch);
        float vertical_distance = left1_4351_43right_f32_f32(displaced_height, camera_y);
        float tmp89 = left1_4321_43right_f32_f32(vertical_distance, pitch_cosine);
        float tmp90 = left1_4321_43right_f32_f32(view_distance, pitch_sine);
        float view_y = left1_4331_43right_f32_f32(tmp89, tmp90);
        float tmp91 = left1_4321_43right_f32_f32(view_distance, pitch_cosine);
        float tmp92 = left1_4321_43right_f32_f32(vertical_distance, pitch_sine);
        float view_z = left1_4351_43right_f32_f32(tmp91, tmp92);
        float tmp93 = 0.7200000286102294921875;
        float tmp94 = left1_4321_43right_f32_f32(aspect, tmp93);
        float tmp95 = 0.7200000286102294921875;
        float tmp96 = 1.0033400058746337890625;
        float tmp97 = left1_4321_43right_f32_f32(view_z, tmp96);
        float tmp98 = 0.400669991970062255859375;
        float tmp99 = 0.064999997615814208984375;
        float tmp100 = left1_4321_43right_f32_f32(world_x, tmp99);
        float tmp101 = 0.064999997615814208984375;
        float tmp102 = left1_4321_43right_f32_f32(world_z, tmp101);
        float tmp103 = 13.69999980926513671875;
        float tmp104 = simplex_field_at_x_y_phase_f32_f32_f32(tmp100, tmp102, tmp103);
        float tmp105 = 0.5;
        float tmp106 = left1_4321_43right_f32_f32(tmp104, tmp105);
        float tmp107 = 0.5;
        float tmp108 = 0.14000000059604644775390625;
        float tmp109 = left1_4321_43right_f32_f32(world_x, tmp108);
        float tmp110 = 0.14000000059604644775390625;
        float tmp111 = left1_4321_43right_f32_f32(world_z, tmp110);
        float tmp112 = 16.200000762939453125;
        float tmp113 = simplex_field_at_x_y_phase_f32_f32_f32(tmp109, tmp111, tmp112);
        float tmp114 = 0.5;
        float tmp115 = left1_4321_43right_f32_f32(tmp113, tmp114);
        float tmp116 = 0.5;
        vec4 _386 = vec4(0.0, 0.0, 0.0, 1.0);
        _386.z = world_z;
        _386.y = displaced_height;
        _386.x = world_x;
        dynlex_interpolant_7465727261696e5f706f736974696f6e = _386;
        vec4 _393 = vec4(0.0);
        _393.w = view_distance;
        _393.z = normal_z;
        _393.y = normal_y;
        _393.x = normal_x;
        dynlex_interpolant_7465727261696e5f6e6f726d616c = _393;
        vec4 _397 = vec4(0.0);
        _397.y = left1_4331_43right_f32_f32(tmp115, tmp116);
        _397.x = left1_4331_43right_f32_f32(tmp106, tmp107);
        dynlex_interpolant_7465727261696e5f6d6174657269616c = _397;
        vec4 _400 = vec4(0.0);
        _400.w = view_z;
        _400.z = left1_4351_43right_f32_f32(tmp97, tmp98);
        _400.y = left1_4371_43right_f32_f32(view_y, tmp95);
        _400.x = left1_4371_43right_f32_f32(lateral_distance, tmp94);
        gl_Position = _400;
    }
}
