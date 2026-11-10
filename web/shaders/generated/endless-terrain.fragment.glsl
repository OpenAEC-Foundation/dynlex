#version 300 es

precision highp float;
precision highp int;

struct type_7 {
    float member;
};
vec4 gen_gl_FragCoord_1 = vec4(0.0);

vec4 gen_gl_FragColor = vec4(0.0);

layout(std140) uniform type_7_block_0Fragment { type_7 _group_0_binding_0_fs; };

layout(std140) uniform type_7_block_1Fragment { type_7 _group_0_binding_2_fs; };

layout(std140) uniform type_7_block_2Fragment { type_7 _group_0_binding_3_fs; };

vec4 dynlex_interpolant_7465727261696e20706f736974696f6e_1 = vec4(0.0);

vec4 dynlex_interpolant_7465727261696e206e6f726d616c_1 = vec4(0.0);

vec4 dynlex_interpolant_7465727261696e206d6174657269616c_1 = vec4(0.0);

smooth in vec4 _vs2fs_location2;
smooth in vec4 _vs2fs_location1;
smooth in vec4 _vs2fs_location0;
layout(location = 0) out vec4 _fs2p_location0;

void main_1() {
    uint phi_392_ = 0u;
    float phi_394_ = 0.0;
    uint phi_404_ = 0u;
    float phi_406_ = 0.0;
    uint phi_432_ = 0u;
    float phi_434_ = 0.0;
    uint phi_439_ = 0u;
    float phi_441_ = 0.0;
    uint phi_452_ = 0u;
    float phi_453_ = 0.0;
    float phi_454_ = 0.0;
    float phi_455_ = 0.0;
    uint phi_456_ = 0u;
    uint phi_458_ = 0u;
    float phi_460_ = 0.0;
    float phi_461_ = 0.0;
    uint phi_469_ = 0u;
    float phi_471_ = 0.0;
    uint phi_483_ = 0u;
    float phi_485_ = 0.0;
    uint phi_494_ = 0u;
    float phi_496_ = 0.0;
    uint phi_509_ = 0u;
    float phi_511_ = 0.0;
    uint phi_522_ = 0u;
    float phi_524_ = 0.0;
    uint phi_589_ = 0u;
    float phi_591_ = 0.0;
    uint phi_637_ = 0u;
    float phi_639_ = 0.0;
    uint phi_697_ = 0u;
    float phi_699_ = 0.0;
    uint phi_719_ = 0u;
    float phi_721_ = 0.0;
    uint phi_753_ = 0u;
    float phi_755_ = 0.0;
    uint phi_773_ = 0u;
    float phi_775_ = 0.0;
    uint phi_789_ = 0u;
    float phi_791_ = 0.0;
    uint phi_802_ = 0u;
    float phi_804_ = 0.0;
    uint phi_844_ = 0u;
    float phi_846_ = 0.0;
    uint phi_899_ = 0u;
    float phi_901_ = 0.0;
    vec3 phi_919_ = vec3(0.0);
    uint phi_924_ = 0u;
    float phi_926_ = 0.0;
    uint phi_964_ = 0u;
    float phi_966_ = 0.0;
    uint phi_975_ = 0u;
    float phi_977_ = 0.0;
    vec3 phi_1004_ = vec3(0.0);
    bool local = false;
    float local_1 = 0.0;
    uint phi_1014_ = 0u;
    float phi_1016_ = 0.0;
    vec3 local_2 = vec3(0.0);
    vec3 local_3 = vec3(0.0);
    vec3 local_4 = vec3(0.0);
    float local_5 = 0.0;
    vec4 _e174 = gen_gl_FragCoord_1;
    vec4 _e176 = gen_gl_FragCoord_1;
    float _e179 = _group_0_binding_0_fs.member;
    float _e181 = _group_0_binding_2_fs.member;
    float _e182 = max(_e181, 1.0);
    float _e184 = _group_0_binding_3_fs.member;
    float _e185 = max(_e184, 1.0);
    float _e186 = (_e182 / _e185);
    float _e190 = (_e186 * (((_e174.x / _e182) * 2.0) + -1.0));
    float _e193 = (1.0 - ((_e176.y / _e185) * 2.0));
    vec4 _e194 = dynlex_interpolant_7465727261696e20706f736974696f6e_1;
    vec4 _e196 = dynlex_interpolant_7465727261696e20706f736974696f6e_1;
    vec4 _e198 = dynlex_interpolant_7465727261696e20706f736974696f6e_1;
    vec4 _e200 = dynlex_interpolant_7465727261696e20706f736974696f6e_1;
    vec4 _e202 = dynlex_interpolant_7465727261696e206e6f726d616c_1;
    vec4 _e203 = dynlex_interpolant_7465727261696e206e6f726d616c_1;
    vec4 _e204 = dynlex_interpolant_7465727261696e206e6f726d616c_1;
    vec4 _e205 = dynlex_interpolant_7465727261696e206e6f726d616c_1;
    vec4 _e207 = dynlex_interpolant_7465727261696e206d6174657269616c_1;
    vec4 _e209 = dynlex_interpolant_7465727261696e206d6174657269616c_1;
    vec4 _e211 = dynlex_interpolant_7465727261696e206d6174657269616c_1;
    float _e213 = (_e190 + -0.52);
    float _e214 = (_e193 + -0.36);
    float _e218 = sqrt(((_e213 * _e213) + (_e214 * _e214)));
    float _e220 = ((_e218 + -0.014) / 0.051);
    if ((_e220 < 0.0)) {
        phi_394_ = 0.0;
    } else {
        phi_392_ = 0u;
        if ((_e220 > 1.0)) {
        } else {
            phi_392_ = 1u;
        }
        uint _e224 = phi_392_;
        phi_394_ = _e220;
        if ((0u == _e224)) {
            phi_394_ = 1.0;
        }
    }
    float _e227 = phi_394_;
    float _e232 = (1.0 - ((_e227 * _e227) * (3.0 - (_e227 * 2.0))));
    float _e234 = ((_e218 + -0.04) / 0.4);
    if ((_e234 < 0.0)) {
        phi_406_ = 0.0;
    } else {
        phi_404_ = 0u;
        if ((_e234 > 1.0)) {
        } else {
            phi_404_ = 1u;
        }
        uint _e238 = phi_404_;
        phi_406_ = _e234;
        if ((0u == _e238)) {
            phi_406_ = 1.0;
        }
    }
    float _e241 = phi_406_;
    float _e246 = (1.0 - ((_e241 * _e241) * (3.0 - (_e241 * 2.0))));
    if ((_e200.w < 0.5)) {
        float _e819 = ((_e193 * 0.48) + 0.43);
        if ((_e819 < 0.0)) {
            phi_926_ = 0.0;
        } else {
            phi_924_ = 0u;
            if ((_e819 > 1.0)) {
            } else {
                phi_924_ = 1u;
            }
            uint _e823 = phi_924_;
            phi_926_ = _e819;
            if ((0u == _e823)) {
                phi_926_ = 1.0;
            }
        }
        float _e826 = phi_926_;
        float _e840 = ((sin(((_e179 * 0.021) + ((_e190 * 1.21) + (_e193 * 1.77)))) * 0.5) + 0.5);
        float _e861 = ((((_e840 + ((cos((((_e193 * 2.93) - (_e190 * 0.67)) - (_e179 * 0.016))) * 0.5) + 0.5)) + (((sin((((_e190 * 2.47) - (_e193 * 4.11)) + (_e840 * 1.8))) * 0.5) + 0.5) * 0.46)) + -1.42) / 0.7600001);
        if ((_e861 < 0.0)) {
            phi_966_ = 0.0;
        } else {
            phi_964_ = 0u;
            if ((_e861 > 1.0)) {
            } else {
                phi_964_ = 1u;
            }
            uint _e865 = phi_964_;
            phi_966_ = _e861;
            if ((0u == _e865)) {
                phi_966_ = 1.0;
            }
        }
        float _e868 = phi_966_;
        float _e874 = ((_e193 + 0.08) / 0.45999998);
        if ((_e874 < 0.0)) {
            phi_977_ = 0.0;
        } else {
            phi_975_ = 0u;
            if ((_e874 > 1.0)) {
            } else {
                phi_975_ = 1u;
            }
            uint _e878 = phi_975_;
            phi_977_ = _e874;
            if ((0u == _e878)) {
                phi_977_ = 1.0;
            }
        }
        float _e881 = phi_977_;
        float _e886 = (((_e868 * _e868) * (3.0 - (_e868 * 2.0))) * ((_e881 * _e881) * (3.0 - (_e881 * 2.0))));
        vec3 _e900 = vec3(((_e232 * 1.36) + ((_e246 * 0.31) + (((_e826 * 0.105) + 0.022) + (_e886 * 0.33)))), vec3(0.0).y, vec3(0.0).z);
        vec3 _e908 = vec3(_e900.x, ((_e232 * 0.96) + ((_e246 * 0.19) + (((_e826 * 0.18) + 0.061) + (_e886 * 0.31)))), _e900.z);
        phi_1004_ = vec3(_e908.x, _e908.y, ((_e232 * 0.55) + ((_e246 * 0.09) + (((_e826 * 0.33) + 0.14) + (_e886 * 0.29)))));
    } else {
        float _e256 = sqrt((((_e202.x * _e202.x) + (_e203.y * _e203.y)) + (_e204.z * _e204.z)));
        float _e257 = (_e202.x / _e256);
        float _e258 = (_e203.y / _e256);
        float _e259 = (_e204.z / _e256);
        float _e264 = (((_e258 * 0.79) - (_e257 * 0.43)) - (_e259 * 0.44));
        if ((_e264 < 0.0)) {
            phi_434_ = 0.0;
        } else {
            phi_432_ = 0u;
            if ((_e264 > 1.0)) {
            } else {
                phi_432_ = 1u;
            }
            uint _e268 = phi_432_;
            phi_434_ = _e264;
            if ((0u == _e268)) {
                phi_434_ = 1.0;
            }
        }
        float _e271 = phi_434_;
        float _e273 = ((_e258 * 0.56) + 0.44);
        if ((_e273 < 0.0)) {
            phi_441_ = 0.0;
        } else {
            phi_439_ = 0u;
            if ((_e273 > 1.0)) {
            } else {
                phi_439_ = 1u;
            }
            uint _e277 = phi_439_;
            phi_441_ = _e273;
            if ((0u == _e277)) {
                phi_441_ = 1.0;
            }
        }
        float _e280 = phi_441_;
        phi_453_ = 0.0;
        phi_454_ = 0.0;
        phi_455_ = 0.0;
        phi_456_ = 0u;
        if ((_e200.w > 1.5)) {
        } else {
            float _e282 = (1.0 - _e258);
            float _e289 = (((_e209.y + -0.32) * 1.35) + (_e282 * 1.7));
            phi_452_ = 1u;
            if ((_e289 < 0.0)) {
            } else {
                phi_452_ = 2u;
            }
            uint _e292 = phi_452_;
            phi_453_ = _e289;
            phi_454_ = ((_e207.x * 0.68) + (_e209.y * 0.32));
            phi_455_ = _e282;
            phi_456_ = _e292;
        }
        float _e294 = phi_453_;
        float _e296 = phi_454_;
        float _e298 = phi_455_;
        uint _e300 = phi_456_;
        switch(int(_e300)) {
            case 2:
            case 1: {
                switch(int(_e300)) {
                    case 1: {
                        phi_461_ = 0.0;
                        break;
                    }
                    case 2: {
                        phi_458_ = 0u;
                        if ((_e294 > 1.0)) {
                        } else {
                            phi_458_ = 1u;
                        }
                        uint _e305 = phi_458_;
                        phi_460_ = _e294;
                        if ((0u == _e305)) {
                            phi_460_ = 1.0;
                        }
                        float _e308 = phi_460_;
                        phi_461_ = _e308;
                        break;
                    }
                    default: {
                        break;
                    }
                }
                float _e310 = phi_461_;
                float _e315 = (((_e298 + ((_e310 + -0.5) * 0.095)) + -0.055) / 0.245);
                if ((_e315 < 0.0)) {
                    phi_471_ = 0.0;
                } else {
                    phi_469_ = 0u;
                    if ((_e315 > 1.0)) {
                    } else {
                        phi_469_ = 1u;
                    }
                    uint _e319 = phi_469_;
                    phi_471_ = _e315;
                    if ((0u == _e319)) {
                        phi_471_ = 1.0;
                    }
                }
                float _e322 = phi_471_;
                float _e326 = ((_e322 * _e322) * (3.0 - (_e322 * 2.0)));
                float _e331 = (((_e196.y + ((_e296 + -0.5) * 0.48)) + -2.75) / 1.6999998);
                if ((_e331 < 0.0)) {
                    phi_485_ = 0.0;
                } else {
                    phi_483_ = 0u;
                    if ((_e331 > 1.0)) {
                    } else {
                        phi_483_ = 1u;
                    }
                    uint _e335 = phi_483_;
                    phi_485_ = _e331;
                    if ((0u == _e335)) {
                        phi_485_ = 1.0;
                    }
                }
                float _e338 = phi_485_;
                float _e344 = ((_e258 + -0.25) / 0.41000003);
                if ((_e344 < 0.0)) {
                    phi_496_ = 0.0;
                } else {
                    phi_494_ = 0u;
                    if ((_e344 > 1.0)) {
                    } else {
                        phi_494_ = 1u;
                    }
                    uint _e348 = phi_494_;
                    phi_496_ = _e344;
                    if ((0u == _e348)) {
                        phi_496_ = 1.0;
                    }
                }
                float _e351 = phi_496_;
                float _e359 = (((_e209.y * 0.12) + 0.88) * (((_e338 * _e338) * (3.0 - (_e338 * 2.0))) * ((_e351 * _e351) * (3.0 - (_e351 * 2.0)))));
                float _e361 = ((_e196.y + -0.35) / 2.3000002);
                if ((_e361 < 0.0)) {
                    phi_511_ = 0.0;
                } else {
                    phi_509_ = 0u;
                    if ((_e361 > 1.0)) {
                    } else {
                        phi_509_ = 1u;
                    }
                    uint _e365 = phi_509_;
                    phi_511_ = _e361;
                    if ((0u == _e365)) {
                        phi_511_ = 1.0;
                    }
                }
                float _e368 = phi_511_;
                float _e372 = ((_e368 * _e368) * (3.0 - (_e368 * 2.0)));
                float _e376 = (((_e257 * 0.38) - (_e259 * 0.26)) + 0.48);
                if ((_e376 < 0.0)) {
                    phi_524_ = 0.0;
                } else {
                    phi_522_ = 0u;
                    if ((_e376 > 1.0)) {
                    } else {
                        phi_522_ = 1u;
                    }
                    uint _e380 = phi_522_;
                    phi_524_ = _e376;
                    if ((0u == _e380)) {
                        phi_524_ = 1.0;
                    }
                }
                float _e383 = phi_524_;
                float _e408 = (1.0 - _e326);
                float _e418 = (1.0 - _e359);
                float _e434 = (((_e209.y * 0.26) + 0.8) * (((_e271 * 1.16) + 0.17) + (_e280 * 0.17)));
                float _e445 = ((_e205.w + -188.0) / 188.0);
                if ((_e445 < 0.0)) {
                    phi_591_ = 0.0;
                } else {
                    phi_589_ = 0u;
                    if ((_e445 > 1.0)) {
                    } else {
                        phi_589_ = 1u;
                    }
                    uint _e449 = phi_589_;
                    phi_591_ = _e445;
                    if ((0u == _e449)) {
                        phi_591_ = 1.0;
                    }
                }
                float _e452 = phi_591_;
                float _e456 = ((_e452 * _e452) * (3.0 - (_e452 * 2.0)));
                float _e463 = (1.0 - _e456);
                vec3 _e470 = vec3(((((_e246 * 0.17) + 0.17) * _e456) + (((_e271 * 0.13) + (_e434 * ((_e359 * 0.78) + (_e418 * ((_e408 * (((_e296 * 0.05) + 0.028) + (_e372 * 0.034))) + (_e326 * (((_e296 * 0.066) + 0.119) + (_e383 * 0.074)))))))) * _e463)), vec3(0.0).y, vec3(0.0).z);
                vec3 _e477 = vec3(_e470.x, ((((_e246 * 0.105) + 0.23) * _e456) + (((_e271 * 0.1) + (_e434 * ((_e359 * 0.83) + (_e418 * ((_e408 * (((_e296 * 0.086) + 0.094) - (_e372 * 0.05))) + (_e326 * (((_e296 * 0.058) + 0.112) + (_e383 * 0.047)))))))) * _e463)), _e470.z);
                phi_919_ = vec3(_e477.x, _e477.y, ((((_e246 * 0.052) + 0.31) * _e456) + (((_e271 * 0.07) + (_e434 * ((_e359 * 0.87) + (_e418 * ((_e408 * (((_e296 * 0.036) + 0.043) + (_e372 * 0.02))) + (_e326 * (((_e296 * 0.054) + 0.118) + (_e383 * 0.037)))))))) * _e463)));
                break;
            }
            default: {
                float _e500 = cos(((_e179 * 1.12) + ((_e194.x * 1.37) + (_e198.z * 0.91))));
                float _e501 = cos((((_e198.z * 1.61) - (_e194.x * 0.83)) - (_e179 * 0.94)));
                float _e502 = cos(((_e179 * 0.71) + ((_e194.x * 2.11) - (_e198.z * 0.57))));
                float _e504 = ((_e205.w + -48.0) / 48.0);
                if ((_e504 < 0.0)) {
                    phi_639_ = 0.0;
                } else {
                    phi_637_ = 0u;
                    if ((_e504 > 1.0)) {
                    } else {
                        phi_637_ = 1u;
                    }
                    uint _e508 = phi_637_;
                    phi_639_ = _e504;
                    if ((0u == _e508)) {
                        phi_639_ = 1.0;
                    }
                }
                float _e511 = phi_639_;
                float _e516 = (1.0 - ((_e511 * _e511) * (3.0 - (_e511 * 2.0))));
                float _e523 = (_e257 - (((_e502 * 0.03195) + ((_e500 * 0.07095) - (_e501 * 0.02835))) * _e516));
                float _e530 = (_e259 - ((((_e500 * 0.05115) + (_e501 * 0.05355)) - (_e502 * 0.00855)) * _e516));
                float _e536 = sqrt(((_e530 * _e530) + ((_e258 * _e258) + (_e523 * _e523))));
                float _e537 = (_e523 / _e536);
                float _e538 = (_e258 / _e536);
                float _e539 = (_e530 / _e536);
                float _e550 = ((((sin((_e179 * 0.091)) * 4.2) + (cos((_e179 * 0.037)) * 1.6)) + 201.7) - _e194.x);
                float _e551 = (12.8 - _e196.y);
                float _e552 = (((_e179 * 3.25) + -92.1) - _e198.z);
                float _e558 = sqrt((((_e550 * _e550) + (_e551 * _e551)) + (_e552 * _e552)));
                float _e559 = (_e550 / _e558);
                float _e560 = (_e551 / _e558);
                float _e561 = (_e552 / _e558);
                float _e566 = ((_e561 * _e539) + ((_e559 * _e537) + (_e560 * _e538)));
                if ((_e566 < 0.0)) {
                    phi_699_ = 0.0;
                } else {
                    phi_697_ = 0u;
                    if ((_e566 > 1.0)) {
                    } else {
                        phi_697_ = 1u;
                    }
                    uint _e570 = phi_697_;
                    phi_699_ = _e566;
                    if ((0u == _e570)) {
                        phi_699_ = 1.0;
                    }
                }
                float _e573 = phi_699_;
                float _e574 = (1.0 - _e573);
                float _e575 = (_e574 * _e574);
                float _e579 = (((_e574 * (_e575 * _e575)) * 0.98) + 0.02);
                float _e582 = (((_e537 * _e573) * 2.0) - _e559);
                float _e585 = (((_e538 * _e573) * 2.0) - _e560);
                float _e588 = (((_e539 * _e573) * 2.0) - _e561);
                float _e590 = ((_e585 * 0.72) + 0.28);
                if ((_e590 < 0.0)) {
                    phi_721_ = 0.0;
                } else {
                    phi_719_ = 0u;
                    if ((_e590 > 1.0)) {
                    } else {
                        phi_719_ = 1u;
                    }
                    uint _e594 = phi_719_;
                    phi_721_ = _e590;
                    if ((0u == _e594)) {
                        phi_721_ = 1.0;
                    }
                }
                float _e597 = phi_721_;
                float _e598 = (1.0 - _e597);
                float _e626 = (((((sin(((_e179 * 0.018) + ((_e582 * 2.1) + (_e588 * 1.3)))) * 0.5) + 0.5) + ((cos((((_e588 * 2.7) - (_e582 * 1.4)) - (_e179 * 0.013))) * 0.5) + 0.5)) + -1.25) / 0.47000003);
                if ((_e626 < 0.0)) {
                    phi_755_ = 0.0;
                } else {
                    phi_753_ = 0u;
                    if ((_e626 > 1.0)) {
                    } else {
                        phi_753_ = 1u;
                    }
                    uint _e630 = phi_753_;
                    phi_755_ = _e626;
                    if ((0u == _e630)) {
                        phi_755_ = 1.0;
                    }
                }
                float _e633 = phi_755_;
                float _e637 = ((_e633 * _e633) * (3.0 - (_e633 * 2.0)));
                float _e648 = ((_e588 * 0.86) + ((_e582 * 0.39) + (_e585 * 0.32)));
                if ((_e648 < 0.0)) {
                    phi_775_ = 0.0;
                } else {
                    phi_773_ = 0u;
                    if ((_e648 > 1.0)) {
                    } else {
                        phi_773_ = 1u;
                    }
                    uint _e652 = phi_773_;
                    phi_775_ = _e648;
                    if ((0u == _e652)) {
                        phi_775_ = 1.0;
                    }
                }
                float _e655 = phi_775_;
                float _e656 = (_e655 * _e655);
                float _e657 = (_e656 * _e656);
                float _e658 = (_e657 * _e657);
                float _e659 = (_e658 * _e658);
                float _e660 = (_e659 * _e659);
                float _e664 = (((_e579 * 0.65) + 0.35) * (_e660 * _e660));
                float _e666 = ((_e211.z + -0.18) / 3.62);
                if ((_e666 < 0.0)) {
                    phi_791_ = 0.0;
                } else {
                    phi_789_ = 0u;
                    if ((_e666 > 1.0)) {
                    } else {
                        phi_789_ = 1u;
                    }
                    uint _e670 = phi_789_;
                    phi_791_ = _e666;
                    if ((0u == _e670)) {
                        phi_791_ = 1.0;
                    }
                }
                float _e673 = phi_791_;
                float _e679 = (_e516 * (1.0 - ((_e673 * _e673) * (3.0 - (_e673 * 2.0)))));
                float _e681 = ((_e205.w + -24.0) / 196.0);
                if ((_e681 < 0.0)) {
                    phi_804_ = 0.0;
                } else {
                    phi_802_ = 0u;
                    if ((_e681 > 1.0)) {
                    } else {
                        phi_802_ = 1u;
                    }
                    uint _e685 = phi_802_;
                    phi_804_ = _e681;
                    if ((0u == _e685)) {
                        phi_804_ = 1.0;
                    }
                }
                float _e688 = phi_804_;
                float _e692 = ((_e688 * _e688) * (3.0 - (_e688 * 2.0)));
                float _e708 = (1.0 - _e679);
                float _e725 = ((_e211.z + -0.05) / 0.59999996);
                if ((_e725 < 0.0)) {
                    phi_846_ = 0.0;
                } else {
                    phi_844_ = 0u;
                    if ((_e725 > 1.0)) {
                    } else {
                        phi_844_ = 1u;
                    }
                    uint _e729 = phi_844_;
                    phi_846_ = _e725;
                    if ((0u == _e729)) {
                        phi_846_ = 1.0;
                    }
                }
                float _e732 = phi_846_;
                float _e744 = (abs(((_e502 * 0.28) + (_e500 + (_e501 * 0.72)))) / 0.18);
                float _e746 = ((_e744 > 1.0) ? 1.0 : _e744);
                float _e752 = ((1.0 - ((_e746 * _e746) * (3.0 - (_e746 * 2.0)))) * (_e516 * (1.0 - ((_e732 * _e732) * (3.0 - (_e732 * 2.0))))));
                float _e759 = (1.0 - _e579);
                float _e782 = ((_e205.w + -232.0) / 144.0);
                if ((_e782 < 0.0)) {
                    phi_901_ = 0.0;
                } else {
                    phi_899_ = 0u;
                    if ((_e782 > 1.0)) {
                    } else {
                        phi_899_ = 1u;
                    }
                    uint _e786 = phi_899_;
                    phi_901_ = _e782;
                    if ((0u == _e786)) {
                        phi_901_ = 1.0;
                    }
                }
                float _e789 = phi_901_;
                float _e793 = ((_e789 * _e789) * (3.0 - (_e789 * 2.0)));
                float _e794 = (1.0 - _e793);
                vec3 _e801 = vec3(((_e793 * 0.17) + (((_e664 * 0.7) + ((_e660 * 0.08) + ((_e579 * (((_e597 * 0.025) + (_e598 * 0.16)) + (_e637 * 0.028))) + (_e759 * (((((_e209.y * 0.012) + 0.018) * _e679) + (_e708 * (((_e207.x * 0.008) + 0.004) * (1.0 - (_e692 * 0.42))))) + (_e752 * 0.006)))))) * _e794)), vec3(0.0).y, vec3(0.0).z);
                vec3 _e808 = vec3(_e801.x, ((_e793 * 0.23) + (((_e664 * 0.42) + ((_e660 * 0.045) + ((_e579 * (((_e597 * 0.095) + (_e598 * 0.24)) + (_e637 * 0.036))) + (_e759 * (((((_e209.y * 0.05) + 0.105) * _e679) + (_e708 * (((_e207.x * 0.038) + 0.018) * (1.0 - (_e692 * 0.28))))) + (_e752 * 0.032)))))) * _e794)), _e801.z);
                phi_919_ = vec3(_e808.x, _e808.y, ((_e793 * 0.31) + (((_e664 * 0.16) + ((_e660 * 0.018) + ((_e579 * (((_e597 * 0.24) + (_e598 * 0.32)) + (_e637 * 0.045))) + (_e759 * (((((_e209.y * 0.055) + 0.125) * _e679) + (_e708 * (((_e207.x * 0.058) + 0.042) * (1.0 - (_e692 * 0.12))))) + (_e752 * 0.04)))))) * _e794)));
                break;
            }
        }
        vec3 _e817 = phi_919_;
        phi_1004_ = _e817;
        vec3 _e918 = phi_1004_;
        float _e919 = (_e190 / _e186);
        float _e925 = ((sqrt(((_e193 * _e193) + (_e919 * _e919))) + -0.58) / 0.76000005);
        local = (_e925 < 0.0);
        local_1 = _e925;
        local_5 = _e925;
        local_2 = _e918;
        local_3 = _e918;
        local_4 = _e918;
    }
    bool _e928 = local;
    if (_e928) {
        phi_1016_ = 0.0;
    } else {
        float _e930 = local_1;
        phi_1014_ = 0u;
        if ((_e930 > 1.0)) {
        } else {
            phi_1014_ = 1u;
        }
        uint _e933 = phi_1014_;
        float _e1039 = local_5;
        phi_1016_ = _e1039;
        if ((0u == _e933)) {
            phi_1016_ = 1.0;
        }
    }
    float _e936 = phi_1016_;
    float _e943 = (((1.0 - ((_e936 * _e936) * (3.0 - (_e936 * 2.0)))) * 0.62) + 0.38);
    vec3 _e945 = local_2;
    vec3 _e952 = local_3;
    vec3 _e959 = local_4;
    vec4 _e969 = vec4(((_e945.x * _e943) / ((_e945.x * 0.38) + 1.0)), vec4(0.0, 0.0, 0.0, 1.0).y, vec4(0.0, 0.0, 0.0, 1.0).z, vec4(0.0, 0.0, 0.0, 1.0).w);
    vec4 _e974 = vec4(_e969.x, ((_e952.y * _e943) / ((_e952.y * 0.38) + 1.0)), _e969.z, _e969.w);
    gen_gl_FragColor = vec4(_e974.x, _e974.y, ((_e959.z * _e943) / ((_e959.z * 0.38) + 1.0)), _e974.w);
    return;
}

void main() {
    vec4 gen_gl_FragCoord = gl_FragCoord;
    vec4 dynlex_interpolant_7465727261696e20706f736974696f6e = _vs2fs_location2;
    vec4 dynlex_interpolant_7465727261696e206e6f726d616c = _vs2fs_location1;
    vec4 dynlex_interpolant_7465727261696e206d6174657269616c = _vs2fs_location0;
    gen_gl_FragCoord_1 = gen_gl_FragCoord;
    dynlex_interpolant_7465727261696e20706f736974696f6e_1 = dynlex_interpolant_7465727261696e20706f736974696f6e;
    dynlex_interpolant_7465727261696e206e6f726d616c_1 = dynlex_interpolant_7465727261696e206e6f726d616c;
    dynlex_interpolant_7465727261696e206d6174657269616c_1 = dynlex_interpolant_7465727261696e206d6174657269616c;
    main_1();
    vec4 _e9 = gen_gl_FragColor;
    _fs2p_location0 = _e9;
    return;
}
