#version 300 es

precision highp float;
precision highp int;

struct type_7 {
    float member;
};
vec4 gen_gl_FragCoord_1 = vec4(0.0);

vec4 gen_gl_FragColor = vec4(0.0);

vec4 dynlex_interpolant_7465727261696e20706f736974696f6e_1 = vec4(0.0);

vec4 dynlex_interpolant_7465727261696e206e6f726d616c_1 = vec4(0.0);

vec4 dynlex_interpolant_7465727261696e206d6174657269616c_1 = vec4(0.0);

layout(std140) uniform type_7_block_0Fragment { type_7 _group_0_binding_0_fs; };

layout(std140) uniform type_7_block_1Fragment { type_7 _group_0_binding_2_fs; };

layout(std140) uniform type_7_block_2Fragment { type_7 _group_0_binding_3_fs; };

smooth in vec4 _vs2fs_location2;
smooth in vec4 _vs2fs_location1;
smooth in vec4 _vs2fs_location0;
layout(location = 0) out vec4 _fs2p_location0;

void main_1() {
    uint phi_384_ = 0u;
    float phi_386_ = 0.0;
    uint phi_396_ = 0u;
    float phi_398_ = 0.0;
    uint phi_424_ = 0u;
    float phi_426_ = 0.0;
    uint phi_431_ = 0u;
    float phi_433_ = 0.0;
    uint phi_444_ = 0u;
    float phi_445_ = 0.0;
    float phi_446_ = 0.0;
    float phi_447_ = 0.0;
    uint phi_448_ = 0u;
    uint phi_450_ = 0u;
    float phi_452_ = 0.0;
    float phi_453_ = 0.0;
    uint phi_461_ = 0u;
    float phi_463_ = 0.0;
    uint phi_475_ = 0u;
    float phi_477_ = 0.0;
    uint phi_486_ = 0u;
    float phi_488_ = 0.0;
    uint phi_501_ = 0u;
    float phi_503_ = 0.0;
    uint phi_514_ = 0u;
    float phi_516_ = 0.0;
    uint phi_581_ = 0u;
    float phi_583_ = 0.0;
    uint phi_629_ = 0u;
    float phi_631_ = 0.0;
    uint phi_689_ = 0u;
    float phi_691_ = 0.0;
    uint phi_711_ = 0u;
    float phi_713_ = 0.0;
    uint phi_745_ = 0u;
    float phi_747_ = 0.0;
    uint phi_765_ = 0u;
    float phi_767_ = 0.0;
    uint phi_781_ = 0u;
    float phi_783_ = 0.0;
    uint phi_794_ = 0u;
    float phi_796_ = 0.0;
    uint phi_836_ = 0u;
    float phi_838_ = 0.0;
    uint phi_891_ = 0u;
    float phi_893_ = 0.0;
    vec3 phi_911_ = vec3(0.0);
    uint phi_916_ = 0u;
    float phi_918_ = 0.0;
    uint phi_956_ = 0u;
    float phi_958_ = 0.0;
    uint phi_967_ = 0u;
    float phi_969_ = 0.0;
    vec3 phi_996_ = vec3(0.0);
    bool local = false;
    float local_1 = 0.0;
    uint phi_1006_ = 0u;
    float phi_1008_ = 0.0;
    vec3 local_2 = vec3(0.0);
    vec3 local_3 = vec3(0.0);
    vec3 local_4 = vec3(0.0);
    float local_5 = 0.0;
    vec4 _e174 = dynlex_interpolant_7465727261696e20706f736974696f6e_1;
    vec4 _e175 = dynlex_interpolant_7465727261696e206e6f726d616c_1;
    vec4 _e176 = dynlex_interpolant_7465727261696e206d6174657269616c_1;
    vec4 _e177 = gen_gl_FragCoord_1;
    vec4 _e179 = gen_gl_FragCoord_1;
    float _e182 = _group_0_binding_0_fs.member;
    float _e184 = _group_0_binding_2_fs.member;
    float _e185 = max(_e184, 1.0);
    float _e187 = _group_0_binding_3_fs.member;
    float _e188 = max(_e187, 1.0);
    float _e189 = (_e185 / _e188);
    float _e193 = (_e189 * (((_e177.x / _e185) * 2.0) + -1.0));
    float _e196 = (1.0 - ((_e179.y / _e188) * 2.0));
    float _e205 = (_e193 + -0.52);
    float _e206 = (_e196 + -0.36);
    float _e210 = sqrt(((_e205 * _e205) + (_e206 * _e206)));
    float _e212 = ((_e210 + -0.014) / 0.051);
    if ((_e212 < 0.0)) {
        phi_386_ = 0.0;
    } else {
        phi_384_ = 0u;
        if ((_e212 > 1.0)) {
        } else {
            phi_384_ = 1u;
        }
        uint _e216 = phi_384_;
        phi_386_ = _e212;
        if ((0u == _e216)) {
            phi_386_ = 1.0;
        }
    }
    float _e219 = phi_386_;
    float _e224 = (1.0 - ((_e219 * _e219) * (3.0 - (_e219 * 2.0))));
    float _e226 = ((_e210 + -0.04) / 0.4);
    if ((_e226 < 0.0)) {
        phi_398_ = 0.0;
    } else {
        phi_396_ = 0u;
        if ((_e226 > 1.0)) {
        } else {
            phi_396_ = 1u;
        }
        uint _e230 = phi_396_;
        phi_398_ = _e226;
        if ((0u == _e230)) {
            phi_398_ = 1.0;
        }
    }
    float _e233 = phi_398_;
    float _e238 = (1.0 - ((_e233 * _e233) * (3.0 - (_e233 * 2.0))));
    if ((_e174.w < 0.5)) {
        float _e811 = ((_e196 * 0.48) + 0.43);
        if ((_e811 < 0.0)) {
            phi_918_ = 0.0;
        } else {
            phi_916_ = 0u;
            if ((_e811 > 1.0)) {
            } else {
                phi_916_ = 1u;
            }
            uint _e815 = phi_916_;
            phi_918_ = _e811;
            if ((0u == _e815)) {
                phi_918_ = 1.0;
            }
        }
        float _e818 = phi_918_;
        float _e832 = ((sin(((_e182 * 0.021) + ((_e193 * 1.21) + (_e196 * 1.77)))) * 0.5) + 0.5);
        float _e853 = ((((_e832 + ((cos((((_e196 * 2.93) - (_e193 * 0.67)) - (_e182 * 0.016))) * 0.5) + 0.5)) + (((sin((((_e193 * 2.47) - (_e196 * 4.11)) + (_e832 * 1.8))) * 0.5) + 0.5) * 0.46)) + -1.42) / 0.7600001);
        if ((_e853 < 0.0)) {
            phi_958_ = 0.0;
        } else {
            phi_956_ = 0u;
            if ((_e853 > 1.0)) {
            } else {
                phi_956_ = 1u;
            }
            uint _e857 = phi_956_;
            phi_958_ = _e853;
            if ((0u == _e857)) {
                phi_958_ = 1.0;
            }
        }
        float _e860 = phi_958_;
        float _e866 = ((_e196 + 0.08) / 0.45999998);
        if ((_e866 < 0.0)) {
            phi_969_ = 0.0;
        } else {
            phi_967_ = 0u;
            if ((_e866 > 1.0)) {
            } else {
                phi_967_ = 1u;
            }
            uint _e870 = phi_967_;
            phi_969_ = _e866;
            if ((0u == _e870)) {
                phi_969_ = 1.0;
            }
        }
        float _e873 = phi_969_;
        float _e878 = (((_e860 * _e860) * (3.0 - (_e860 * 2.0))) * ((_e873 * _e873) * (3.0 - (_e873 * 2.0))));
        vec3 _e892 = vec3(((_e224 * 1.36) + ((_e238 * 0.31) + (((_e818 * 0.105) + 0.022) + (_e878 * 0.33)))), vec3(0.0).y, vec3(0.0).z);
        vec3 _e900 = vec3(_e892.x, ((_e224 * 0.96) + ((_e238 * 0.19) + (((_e818 * 0.18) + 0.061) + (_e878 * 0.31)))), _e892.z);
        phi_996_ = vec3(_e900.x, _e900.y, ((_e224 * 0.55) + ((_e238 * 0.09) + (((_e818 * 0.33) + 0.14) + (_e878 * 0.29)))));
    } else {
        float _e248 = sqrt(((_e175.z * _e175.z) + ((_e175.x * _e175.x) + (_e175.y * _e175.y))));
        float _e249 = (_e175.x / _e248);
        float _e250 = (_e175.y / _e248);
        float _e251 = (_e175.z / _e248);
        float _e256 = (((_e250 * 0.79) - (_e249 * 0.43)) - (_e251 * 0.44));
        if ((_e256 < 0.0)) {
            phi_426_ = 0.0;
        } else {
            phi_424_ = 0u;
            if ((_e256 > 1.0)) {
            } else {
                phi_424_ = 1u;
            }
            uint _e260 = phi_424_;
            phi_426_ = _e256;
            if ((0u == _e260)) {
                phi_426_ = 1.0;
            }
        }
        float _e263 = phi_426_;
        float _e265 = ((_e250 * 0.56) + 0.44);
        if ((_e265 < 0.0)) {
            phi_433_ = 0.0;
        } else {
            phi_431_ = 0u;
            if ((_e265 > 1.0)) {
            } else {
                phi_431_ = 1u;
            }
            uint _e269 = phi_431_;
            phi_433_ = _e265;
            if ((0u == _e269)) {
                phi_433_ = 1.0;
            }
        }
        float _e272 = phi_433_;
        phi_445_ = 0.0;
        phi_446_ = 0.0;
        phi_447_ = 0.0;
        phi_448_ = 0u;
        if ((_e174.w > 1.5)) {
        } else {
            float _e274 = (1.0 - _e250);
            float _e281 = (((_e176.y + -0.32) * 1.35) + (_e274 * 1.7));
            phi_444_ = 1u;
            if ((_e281 < 0.0)) {
            } else {
                phi_444_ = 2u;
            }
            uint _e284 = phi_444_;
            phi_445_ = _e281;
            phi_446_ = ((_e176.x * 0.68) + (_e176.y * 0.32));
            phi_447_ = _e274;
            phi_448_ = _e284;
        }
        float _e286 = phi_445_;
        float _e288 = phi_446_;
        float _e290 = phi_447_;
        uint _e292 = phi_448_;
        switch(int(_e292)) {
            case 2:
            case 1: {
                switch(int(_e292)) {
                    case 1: {
                        phi_453_ = 0.0;
                        break;
                    }
                    case 2: {
                        phi_450_ = 0u;
                        if ((_e286 > 1.0)) {
                        } else {
                            phi_450_ = 1u;
                        }
                        uint _e297 = phi_450_;
                        phi_452_ = _e286;
                        if ((0u == _e297)) {
                            phi_452_ = 1.0;
                        }
                        float _e300 = phi_452_;
                        phi_453_ = _e300;
                        break;
                    }
                    default: {
                        break;
                    }
                }
                float _e302 = phi_453_;
                float _e307 = (((_e290 + ((_e302 + -0.5) * 0.095)) + -0.055) / 0.245);
                if ((_e307 < 0.0)) {
                    phi_463_ = 0.0;
                } else {
                    phi_461_ = 0u;
                    if ((_e307 > 1.0)) {
                    } else {
                        phi_461_ = 1u;
                    }
                    uint _e311 = phi_461_;
                    phi_463_ = _e307;
                    if ((0u == _e311)) {
                        phi_463_ = 1.0;
                    }
                }
                float _e314 = phi_463_;
                float _e318 = ((_e314 * _e314) * (3.0 - (_e314 * 2.0)));
                float _e323 = (((_e174.y + ((_e288 + -0.5) * 0.48)) + -2.75) / 1.6999998);
                if ((_e323 < 0.0)) {
                    phi_477_ = 0.0;
                } else {
                    phi_475_ = 0u;
                    if ((_e323 > 1.0)) {
                    } else {
                        phi_475_ = 1u;
                    }
                    uint _e327 = phi_475_;
                    phi_477_ = _e323;
                    if ((0u == _e327)) {
                        phi_477_ = 1.0;
                    }
                }
                float _e330 = phi_477_;
                float _e336 = ((_e250 + -0.25) / 0.41000003);
                if ((_e336 < 0.0)) {
                    phi_488_ = 0.0;
                } else {
                    phi_486_ = 0u;
                    if ((_e336 > 1.0)) {
                    } else {
                        phi_486_ = 1u;
                    }
                    uint _e340 = phi_486_;
                    phi_488_ = _e336;
                    if ((0u == _e340)) {
                        phi_488_ = 1.0;
                    }
                }
                float _e343 = phi_488_;
                float _e351 = (((_e176.y * 0.12) + 0.88) * (((_e330 * _e330) * (3.0 - (_e330 * 2.0))) * ((_e343 * _e343) * (3.0 - (_e343 * 2.0)))));
                float _e353 = ((_e174.y + -0.35) / 2.3000002);
                if ((_e353 < 0.0)) {
                    phi_503_ = 0.0;
                } else {
                    phi_501_ = 0u;
                    if ((_e353 > 1.0)) {
                    } else {
                        phi_501_ = 1u;
                    }
                    uint _e357 = phi_501_;
                    phi_503_ = _e353;
                    if ((0u == _e357)) {
                        phi_503_ = 1.0;
                    }
                }
                float _e360 = phi_503_;
                float _e364 = ((_e360 * _e360) * (3.0 - (_e360 * 2.0)));
                float _e368 = (((_e249 * 0.38) - (_e251 * 0.26)) + 0.48);
                if ((_e368 < 0.0)) {
                    phi_516_ = 0.0;
                } else {
                    phi_514_ = 0u;
                    if ((_e368 > 1.0)) {
                    } else {
                        phi_514_ = 1u;
                    }
                    uint _e372 = phi_514_;
                    phi_516_ = _e368;
                    if ((0u == _e372)) {
                        phi_516_ = 1.0;
                    }
                }
                float _e375 = phi_516_;
                float _e400 = (1.0 - _e318);
                float _e410 = (1.0 - _e351);
                float _e426 = (((_e176.y * 0.26) + 0.8) * (((_e263 * 1.16) + 0.17) + (_e272 * 0.17)));
                float _e437 = ((_e175.w + -188.0) / 188.0);
                if ((_e437 < 0.0)) {
                    phi_583_ = 0.0;
                } else {
                    phi_581_ = 0u;
                    if ((_e437 > 1.0)) {
                    } else {
                        phi_581_ = 1u;
                    }
                    uint _e441 = phi_581_;
                    phi_583_ = _e437;
                    if ((0u == _e441)) {
                        phi_583_ = 1.0;
                    }
                }
                float _e444 = phi_583_;
                float _e448 = ((_e444 * _e444) * (3.0 - (_e444 * 2.0)));
                float _e455 = (1.0 - _e448);
                vec3 _e462 = vec3(((((_e238 * 0.17) + 0.17) * _e448) + (((_e263 * 0.13) + (_e426 * ((_e351 * 0.78) + (_e410 * ((_e400 * (((_e288 * 0.05) + 0.028) + (_e364 * 0.034))) + (_e318 * (((_e288 * 0.066) + 0.119) + (_e375 * 0.074)))))))) * _e455)), vec3(0.0).y, vec3(0.0).z);
                vec3 _e469 = vec3(_e462.x, ((((_e238 * 0.105) + 0.23) * _e448) + (((_e263 * 0.1) + (_e426 * ((_e351 * 0.83) + (_e410 * ((_e400 * (((_e288 * 0.086) + 0.094) - (_e364 * 0.05))) + (_e318 * (((_e288 * 0.058) + 0.112) + (_e375 * 0.047)))))))) * _e455)), _e462.z);
                phi_911_ = vec3(_e469.x, _e469.y, ((((_e238 * 0.052) + 0.31) * _e448) + (((_e263 * 0.07) + (_e426 * ((_e351 * 0.87) + (_e410 * ((_e400 * (((_e288 * 0.036) + 0.043) + (_e364 * 0.02))) + (_e318 * (((_e288 * 0.054) + 0.118) + (_e375 * 0.037)))))))) * _e455)));
                break;
            }
            default: {
                float _e492 = cos((((_e174.x * 1.37) + (_e174.z * 0.91)) + (_e182 * 1.12)));
                float _e493 = cos((((_e174.z * 1.61) - (_e174.x * 0.83)) - (_e182 * 0.94)));
                float _e494 = cos((((_e174.x * 2.11) - (_e174.z * 0.57)) + (_e182 * 0.71)));
                float _e496 = ((_e175.w + -48.0) / 48.0);
                if ((_e496 < 0.0)) {
                    phi_631_ = 0.0;
                } else {
                    phi_629_ = 0u;
                    if ((_e496 > 1.0)) {
                    } else {
                        phi_629_ = 1u;
                    }
                    uint _e500 = phi_629_;
                    phi_631_ = _e496;
                    if ((0u == _e500)) {
                        phi_631_ = 1.0;
                    }
                }
                float _e503 = phi_631_;
                float _e508 = (1.0 - ((_e503 * _e503) * (3.0 - (_e503 * 2.0))));
                float _e515 = (_e249 - (((_e494 * 0.03195) + ((_e492 * 0.07095) - (_e493 * 0.02835))) * _e508));
                float _e522 = (_e251 - ((((_e492 * 0.05115) + (_e493 * 0.05355)) - (_e494 * 0.00855)) * _e508));
                float _e528 = sqrt(((_e522 * _e522) + ((_e250 * _e250) + (_e515 * _e515))));
                float _e529 = (_e515 / _e528);
                float _e530 = (_e250 / _e528);
                float _e531 = (_e522 / _e528);
                float _e542 = ((((sin((_e182 * 0.091)) * 4.2) + (cos((_e182 * 0.037)) * 1.6)) + 201.7) - _e174.x);
                float _e543 = (12.8 - _e174.y);
                float _e544 = (((_e182 * 3.25) + -92.1) - _e174.z);
                float _e550 = sqrt(((_e544 * _e544) + ((_e543 * _e543) + (_e542 * _e542))));
                float _e551 = (_e542 / _e550);
                float _e552 = (_e543 / _e550);
                float _e553 = (_e544 / _e550);
                float _e558 = ((_e553 * _e531) + ((_e551 * _e529) + (_e552 * _e530)));
                if ((_e558 < 0.0)) {
                    phi_691_ = 0.0;
                } else {
                    phi_689_ = 0u;
                    if ((_e558 > 1.0)) {
                    } else {
                        phi_689_ = 1u;
                    }
                    uint _e562 = phi_689_;
                    phi_691_ = _e558;
                    if ((0u == _e562)) {
                        phi_691_ = 1.0;
                    }
                }
                float _e565 = phi_691_;
                float _e566 = (1.0 - _e565);
                float _e567 = (_e566 * _e566);
                float _e571 = (((_e566 * (_e567 * _e567)) * 0.98) + 0.02);
                float _e574 = (((_e529 * _e565) * 2.0) - _e551);
                float _e577 = (((_e530 * _e565) * 2.0) - _e552);
                float _e580 = (((_e531 * _e565) * 2.0) - _e553);
                float _e582 = ((_e577 * 0.72) + 0.28);
                if ((_e582 < 0.0)) {
                    phi_713_ = 0.0;
                } else {
                    phi_711_ = 0u;
                    if ((_e582 > 1.0)) {
                    } else {
                        phi_711_ = 1u;
                    }
                    uint _e586 = phi_711_;
                    phi_713_ = _e582;
                    if ((0u == _e586)) {
                        phi_713_ = 1.0;
                    }
                }
                float _e589 = phi_713_;
                float _e590 = (1.0 - _e589);
                float _e618 = (((((sin(((_e182 * 0.018) + ((_e574 * 2.1) + (_e580 * 1.3)))) * 0.5) + 0.5) + ((cos((((_e580 * 2.7) - (_e574 * 1.4)) - (_e182 * 0.013))) * 0.5) + 0.5)) + -1.25) / 0.47000003);
                if ((_e618 < 0.0)) {
                    phi_747_ = 0.0;
                } else {
                    phi_745_ = 0u;
                    if ((_e618 > 1.0)) {
                    } else {
                        phi_745_ = 1u;
                    }
                    uint _e622 = phi_745_;
                    phi_747_ = _e618;
                    if ((0u == _e622)) {
                        phi_747_ = 1.0;
                    }
                }
                float _e625 = phi_747_;
                float _e629 = ((_e625 * _e625) * (3.0 - (_e625 * 2.0)));
                float _e640 = ((_e580 * 0.86) + ((_e574 * 0.39) + (_e577 * 0.32)));
                if ((_e640 < 0.0)) {
                    phi_767_ = 0.0;
                } else {
                    phi_765_ = 0u;
                    if ((_e640 > 1.0)) {
                    } else {
                        phi_765_ = 1u;
                    }
                    uint _e644 = phi_765_;
                    phi_767_ = _e640;
                    if ((0u == _e644)) {
                        phi_767_ = 1.0;
                    }
                }
                float _e647 = phi_767_;
                float _e648 = (_e647 * _e647);
                float _e649 = (_e648 * _e648);
                float _e650 = (_e649 * _e649);
                float _e651 = (_e650 * _e650);
                float _e652 = (_e651 * _e651);
                float _e656 = (((_e571 * 0.65) + 0.35) * (_e652 * _e652));
                float _e658 = ((_e176.z + -0.18) / 3.62);
                if ((_e658 < 0.0)) {
                    phi_783_ = 0.0;
                } else {
                    phi_781_ = 0u;
                    if ((_e658 > 1.0)) {
                    } else {
                        phi_781_ = 1u;
                    }
                    uint _e662 = phi_781_;
                    phi_783_ = _e658;
                    if ((0u == _e662)) {
                        phi_783_ = 1.0;
                    }
                }
                float _e665 = phi_783_;
                float _e671 = (_e508 * (1.0 - ((_e665 * _e665) * (3.0 - (_e665 * 2.0)))));
                float _e673 = ((_e175.w + -24.0) / 196.0);
                if ((_e673 < 0.0)) {
                    phi_796_ = 0.0;
                } else {
                    phi_794_ = 0u;
                    if ((_e673 > 1.0)) {
                    } else {
                        phi_794_ = 1u;
                    }
                    uint _e677 = phi_794_;
                    phi_796_ = _e673;
                    if ((0u == _e677)) {
                        phi_796_ = 1.0;
                    }
                }
                float _e680 = phi_796_;
                float _e684 = ((_e680 * _e680) * (3.0 - (_e680 * 2.0)));
                float _e700 = (1.0 - _e671);
                float _e717 = ((_e176.z + -0.05) / 0.59999996);
                if ((_e717 < 0.0)) {
                    phi_838_ = 0.0;
                } else {
                    phi_836_ = 0u;
                    if ((_e717 > 1.0)) {
                    } else {
                        phi_836_ = 1u;
                    }
                    uint _e721 = phi_836_;
                    phi_838_ = _e717;
                    if ((0u == _e721)) {
                        phi_838_ = 1.0;
                    }
                }
                float _e724 = phi_838_;
                float _e736 = (abs(((_e494 * 0.28) + (_e492 + (_e493 * 0.72)))) / 0.18);
                float _e738 = ((_e736 > 1.0) ? 1.0 : _e736);
                float _e744 = ((1.0 - ((_e738 * _e738) * (3.0 - (_e738 * 2.0)))) * (_e508 * (1.0 - ((_e724 * _e724) * (3.0 - (_e724 * 2.0))))));
                float _e751 = (1.0 - _e571);
                float _e774 = ((_e175.w + -232.0) / 144.0);
                if ((_e774 < 0.0)) {
                    phi_893_ = 0.0;
                } else {
                    phi_891_ = 0u;
                    if ((_e774 > 1.0)) {
                    } else {
                        phi_891_ = 1u;
                    }
                    uint _e778 = phi_891_;
                    phi_893_ = _e774;
                    if ((0u == _e778)) {
                        phi_893_ = 1.0;
                    }
                }
                float _e781 = phi_893_;
                float _e785 = ((_e781 * _e781) * (3.0 - (_e781 * 2.0)));
                float _e786 = (1.0 - _e785);
                vec3 _e793 = vec3(((_e785 * 0.17) + (((_e656 * 0.7) + ((_e652 * 0.08) + ((_e571 * (((_e589 * 0.025) + (_e590 * 0.16)) + (_e629 * 0.028))) + (_e751 * (((((_e176.y * 0.012) + 0.018) * _e671) + (_e700 * (((_e176.x * 0.008) + 0.004) * (1.0 - (_e684 * 0.42))))) + (_e744 * 0.006)))))) * _e786)), vec3(0.0).y, vec3(0.0).z);
                vec3 _e800 = vec3(_e793.x, ((_e785 * 0.23) + (((_e656 * 0.42) + ((_e652 * 0.045) + ((_e571 * (((_e589 * 0.095) + (_e590 * 0.24)) + (_e629 * 0.036))) + (_e751 * (((((_e176.y * 0.05) + 0.105) * _e671) + (_e700 * (((_e176.x * 0.038) + 0.018) * (1.0 - (_e684 * 0.28))))) + (_e744 * 0.032)))))) * _e786)), _e793.z);
                phi_911_ = vec3(_e800.x, _e800.y, ((_e785 * 0.31) + (((_e656 * 0.16) + ((_e652 * 0.018) + ((_e571 * (((_e589 * 0.24) + (_e590 * 0.32)) + (_e629 * 0.045))) + (_e751 * (((((_e176.y * 0.055) + 0.125) * _e671) + (_e700 * (((_e176.x * 0.058) + 0.042) * (1.0 - (_e684 * 0.12))))) + (_e744 * 0.04)))))) * _e786)));
                break;
            }
        }
        vec3 _e809 = phi_911_;
        phi_996_ = _e809;
        vec3 _e910 = phi_996_;
        float _e911 = (_e193 / _e189);
        float _e917 = ((sqrt(((_e196 * _e196) + (_e911 * _e911))) + -0.58) / 0.76000005);
        local = (_e917 < 0.0);
        local_1 = _e917;
        local_5 = _e917;
        local_2 = _e910;
        local_3 = _e910;
        local_4 = _e910;
    }
    bool _e920 = local;
    if (_e920) {
        phi_1008_ = 0.0;
    } else {
        float _e922 = local_1;
        phi_1006_ = 0u;
        if ((_e922 > 1.0)) {
        } else {
            phi_1006_ = 1u;
        }
        uint _e925 = phi_1006_;
        float _e1031 = local_5;
        phi_1008_ = _e1031;
        if ((0u == _e925)) {
            phi_1008_ = 1.0;
        }
    }
    float _e928 = phi_1008_;
    float _e935 = (((1.0 - ((_e928 * _e928) * (3.0 - (_e928 * 2.0)))) * 0.62) + 0.38);
    vec3 _e937 = local_2;
    vec3 _e944 = local_3;
    vec3 _e951 = local_4;
    vec4 _e961 = vec4(((_e937.x * _e935) / ((_e937.x * 0.38) + 1.0)), vec4(0.0, 0.0, 0.0, 1.0).y, vec4(0.0, 0.0, 0.0, 1.0).z, vec4(0.0, 0.0, 0.0, 1.0).w);
    vec4 _e966 = vec4(_e961.x, ((_e944.y * _e935) / ((_e944.y * 0.38) + 1.0)), _e961.z, _e961.w);
    gen_gl_FragColor = vec4(_e966.x, _e966.y, ((_e951.z * _e935) / ((_e951.z * 0.38) + 1.0)), _e966.w);
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
