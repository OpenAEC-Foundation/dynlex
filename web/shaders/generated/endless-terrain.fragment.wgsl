struct type_7 {
    member: f32,
}

var<private> gl_FragCoord_1: vec4<f32>;
var<private> gl_FragColor: vec4<f32>;
var<private> dynlex_interpolant_7465727261696e20706f736974696f6e_1: vec4<f32>;
var<private> dynlex_interpolant_7465727261696e206e6f726d616c_1: vec4<f32>;
var<private> dynlex_interpolant_7465727261696e206d6174657269616c_1: vec4<f32>;
@group(0) @binding(0) 
var<uniform> ubo_time: type_7;
@group(0) @binding(2) 
var<uniform> ubo_width: type_7;
@group(0) @binding(3) 
var<uniform> ubo_height: type_7;

fn main_1() {
    var phi_384_: u32;
    var phi_386_: f32;
    var phi_396_: u32;
    var phi_398_: f32;
    var phi_424_: u32;
    var phi_426_: f32;
    var phi_431_: u32;
    var phi_433_: f32;
    var phi_444_: u32;
    var phi_445_: f32;
    var phi_446_: f32;
    var phi_447_: f32;
    var phi_448_: u32;
    var phi_450_: u32;
    var phi_452_: f32;
    var phi_453_: f32;
    var phi_461_: u32;
    var phi_463_: f32;
    var phi_475_: u32;
    var phi_477_: f32;
    var phi_486_: u32;
    var phi_488_: f32;
    var phi_501_: u32;
    var phi_503_: f32;
    var phi_514_: u32;
    var phi_516_: f32;
    var phi_581_: u32;
    var phi_583_: f32;
    var phi_629_: u32;
    var phi_631_: f32;
    var phi_689_: u32;
    var phi_691_: f32;
    var phi_711_: u32;
    var phi_713_: f32;
    var phi_745_: u32;
    var phi_747_: f32;
    var phi_765_: u32;
    var phi_767_: f32;
    var phi_781_: u32;
    var phi_783_: f32;
    var phi_794_: u32;
    var phi_796_: f32;
    var phi_836_: u32;
    var phi_838_: f32;
    var phi_891_: u32;
    var phi_893_: f32;
    var phi_911_: vec3<f32>;
    var phi_916_: u32;
    var phi_918_: f32;
    var phi_956_: u32;
    var phi_958_: f32;
    var phi_967_: u32;
    var phi_969_: f32;
    var phi_996_: vec3<f32>;
    var local: bool;
    var local_1: f32;
    var phi_1006_: u32;
    var phi_1008_: f32;
    var local_2: vec3<f32>;
    var local_3: vec3<f32>;
    var local_4: vec3<f32>;
    var local_5: f32;

    let _e174 = dynlex_interpolant_7465727261696e20706f736974696f6e_1;
    let _e175 = dynlex_interpolant_7465727261696e206e6f726d616c_1;
    let _e176 = dynlex_interpolant_7465727261696e206d6174657269616c_1;
    let _e177 = gl_FragCoord_1;
    let _e179 = gl_FragCoord_1;
    let _e182 = ubo_time.member;
    let _e184 = ubo_width.member;
    let _e185 = max(_e184, 1f);
    let _e187 = ubo_height.member;
    let _e188 = max(_e187, 1f);
    let _e189 = (_e185 / _e188);
    let _e193 = (_e189 * (((_e177.x / _e185) * 2f) + -1f));
    let _e196 = (1f - ((_e179.y / _e188) * 2f));
    let _e205 = (_e193 + -0.52f);
    let _e206 = (_e196 + -0.36f);
    let _e210 = sqrt(((_e205 * _e205) + (_e206 * _e206)));
    let _e212 = ((_e210 + -0.014f) / 0.051f);
    if (_e212 < 0f) {
        phi_386_ = 0f;
    } else {
        phi_384_ = 0u;
        if (_e212 > 1f) {
        } else {
            phi_384_ = 1u;
        }
        let _e216 = phi_384_;
        phi_386_ = _e212;
        if (0u == _e216) {
            phi_386_ = 1f;
        }
    }
    let _e219 = phi_386_;
    let _e224 = (1f - ((_e219 * _e219) * (3f - (_e219 * 2f))));
    let _e226 = ((_e210 + -0.04f) / 0.4f);
    if (_e226 < 0f) {
        phi_398_ = 0f;
    } else {
        phi_396_ = 0u;
        if (_e226 > 1f) {
        } else {
            phi_396_ = 1u;
        }
        let _e230 = phi_396_;
        phi_398_ = _e226;
        if (0u == _e230) {
            phi_398_ = 1f;
        }
    }
    let _e233 = phi_398_;
    let _e238 = (1f - ((_e233 * _e233) * (3f - (_e233 * 2f))));
    if (_e174.w < 0.5f) {
        let _e811 = ((_e196 * 0.48f) + 0.43f);
        if (_e811 < 0f) {
            phi_918_ = 0f;
        } else {
            phi_916_ = 0u;
            if (_e811 > 1f) {
            } else {
                phi_916_ = 1u;
            }
            let _e815 = phi_916_;
            phi_918_ = _e811;
            if (0u == _e815) {
                phi_918_ = 1f;
            }
        }
        let _e818 = phi_918_;
        let _e832 = ((sin(((_e182 * 0.021f) + ((_e193 * 1.21f) + (_e196 * 1.77f)))) * 0.5f) + 0.5f);
        let _e853 = ((((_e832 + ((cos((((_e196 * 2.93f) - (_e193 * 0.67f)) - (_e182 * 0.016f))) * 0.5f) + 0.5f)) + (((sin((((_e193 * 2.47f) - (_e196 * 4.11f)) + (_e832 * 1.8f))) * 0.5f) + 0.5f) * 0.46f)) + -1.42f) / 0.7600001f);
        if (_e853 < 0f) {
            phi_958_ = 0f;
        } else {
            phi_956_ = 0u;
            if (_e853 > 1f) {
            } else {
                phi_956_ = 1u;
            }
            let _e857 = phi_956_;
            phi_958_ = _e853;
            if (0u == _e857) {
                phi_958_ = 1f;
            }
        }
        let _e860 = phi_958_;
        let _e866 = ((_e196 + 0.08f) / 0.45999998f);
        if (_e866 < 0f) {
            phi_969_ = 0f;
        } else {
            phi_967_ = 0u;
            if (_e866 > 1f) {
            } else {
                phi_967_ = 1u;
            }
            let _e870 = phi_967_;
            phi_969_ = _e866;
            if (0u == _e870) {
                phi_969_ = 1f;
            }
        }
        let _e873 = phi_969_;
        let _e878 = (((_e860 * _e860) * (3f - (_e860 * 2f))) * ((_e873 * _e873) * (3f - (_e873 * 2f))));
        let _e892 = vec3<f32>(((_e224 * 1.36f) + ((_e238 * 0.31f) + (((_e818 * 0.105f) + 0.022f) + (_e878 * 0.33f)))), vec3<f32>().y, vec3<f32>().z);
        let _e900 = vec3<f32>(_e892.x, ((_e224 * 0.96f) + ((_e238 * 0.19f) + (((_e818 * 0.18f) + 0.061f) + (_e878 * 0.31f)))), _e892.z);
        phi_996_ = vec3<f32>(_e900.x, _e900.y, ((_e224 * 0.55f) + ((_e238 * 0.09f) + (((_e818 * 0.33f) + 0.14f) + (_e878 * 0.29f)))));
    } else {
        let _e248 = sqrt(((_e175.z * _e175.z) + ((_e175.x * _e175.x) + (_e175.y * _e175.y))));
        let _e249 = (_e175.x / _e248);
        let _e250 = (_e175.y / _e248);
        let _e251 = (_e175.z / _e248);
        let _e256 = (((_e250 * 0.79f) - (_e249 * 0.43f)) - (_e251 * 0.44f));
        if (_e256 < 0f) {
            phi_426_ = 0f;
        } else {
            phi_424_ = 0u;
            if (_e256 > 1f) {
            } else {
                phi_424_ = 1u;
            }
            let _e260 = phi_424_;
            phi_426_ = _e256;
            if (0u == _e260) {
                phi_426_ = 1f;
            }
        }
        let _e263 = phi_426_;
        let _e265 = ((_e250 * 0.56f) + 0.44f);
        if (_e265 < 0f) {
            phi_433_ = 0f;
        } else {
            phi_431_ = 0u;
            if (_e265 > 1f) {
            } else {
                phi_431_ = 1u;
            }
            let _e269 = phi_431_;
            phi_433_ = _e265;
            if (0u == _e269) {
                phi_433_ = 1f;
            }
        }
        let _e272 = phi_433_;
        phi_445_ = f32();
        phi_446_ = f32();
        phi_447_ = f32();
        phi_448_ = 0u;
        if (_e174.w > 1.5f) {
        } else {
            let _e274 = (1f - _e250);
            let _e281 = (((_e176.y + -0.32f) * 1.35f) + (_e274 * 1.7f));
            phi_444_ = 1u;
            if (_e281 < 0f) {
            } else {
                phi_444_ = 2u;
            }
            let _e284 = phi_444_;
            phi_445_ = _e281;
            phi_446_ = ((_e176.x * 0.68f) + (_e176.y * 0.32f));
            phi_447_ = _e274;
            phi_448_ = _e284;
        }
        let _e286 = phi_445_;
        let _e288 = phi_446_;
        let _e290 = phi_447_;
        let _e292 = phi_448_;
        switch bitcast<i32>(_e292) {
            case 2, 1: {
                switch bitcast<i32>(_e292) {
                    case 1: {
                        phi_453_ = 0f;
                        break;
                    }
                    case 2: {
                        phi_450_ = 0u;
                        if (_e286 > 1f) {
                        } else {
                            phi_450_ = 1u;
                        }
                        let _e297 = phi_450_;
                        phi_452_ = _e286;
                        if (0u == _e297) {
                            phi_452_ = 1f;
                        }
                        let _e300 = phi_452_;
                        phi_453_ = _e300;
                        break;
                    }
                    default: {
                    }
                }
                let _e302 = phi_453_;
                let _e307 = (((_e290 + ((_e302 + -0.5f) * 0.095f)) + -0.055f) / 0.245f);
                if (_e307 < 0f) {
                    phi_463_ = 0f;
                } else {
                    phi_461_ = 0u;
                    if (_e307 > 1f) {
                    } else {
                        phi_461_ = 1u;
                    }
                    let _e311 = phi_461_;
                    phi_463_ = _e307;
                    if (0u == _e311) {
                        phi_463_ = 1f;
                    }
                }
                let _e314 = phi_463_;
                let _e318 = ((_e314 * _e314) * (3f - (_e314 * 2f)));
                let _e323 = (((_e174.y + ((_e288 + -0.5f) * 0.48f)) + -2.75f) / 1.6999998f);
                if (_e323 < 0f) {
                    phi_477_ = 0f;
                } else {
                    phi_475_ = 0u;
                    if (_e323 > 1f) {
                    } else {
                        phi_475_ = 1u;
                    }
                    let _e327 = phi_475_;
                    phi_477_ = _e323;
                    if (0u == _e327) {
                        phi_477_ = 1f;
                    }
                }
                let _e330 = phi_477_;
                let _e336 = ((_e250 + -0.25f) / 0.41000003f);
                if (_e336 < 0f) {
                    phi_488_ = 0f;
                } else {
                    phi_486_ = 0u;
                    if (_e336 > 1f) {
                    } else {
                        phi_486_ = 1u;
                    }
                    let _e340 = phi_486_;
                    phi_488_ = _e336;
                    if (0u == _e340) {
                        phi_488_ = 1f;
                    }
                }
                let _e343 = phi_488_;
                let _e351 = (((_e176.y * 0.12f) + 0.88f) * (((_e330 * _e330) * (3f - (_e330 * 2f))) * ((_e343 * _e343) * (3f - (_e343 * 2f)))));
                let _e353 = ((_e174.y + -0.35f) / 2.3000002f);
                if (_e353 < 0f) {
                    phi_503_ = 0f;
                } else {
                    phi_501_ = 0u;
                    if (_e353 > 1f) {
                    } else {
                        phi_501_ = 1u;
                    }
                    let _e357 = phi_501_;
                    phi_503_ = _e353;
                    if (0u == _e357) {
                        phi_503_ = 1f;
                    }
                }
                let _e360 = phi_503_;
                let _e364 = ((_e360 * _e360) * (3f - (_e360 * 2f)));
                let _e368 = (((_e249 * 0.38f) - (_e251 * 0.26f)) + 0.48f);
                if (_e368 < 0f) {
                    phi_516_ = 0f;
                } else {
                    phi_514_ = 0u;
                    if (_e368 > 1f) {
                    } else {
                        phi_514_ = 1u;
                    }
                    let _e372 = phi_514_;
                    phi_516_ = _e368;
                    if (0u == _e372) {
                        phi_516_ = 1f;
                    }
                }
                let _e375 = phi_516_;
                let _e400 = (1f - _e318);
                let _e410 = (1f - _e351);
                let _e426 = (((_e176.y * 0.26f) + 0.8f) * (((_e263 * 1.16f) + 0.17f) + (_e272 * 0.17f)));
                let _e437 = ((_e175.w + -188f) / 188f);
                if (_e437 < 0f) {
                    phi_583_ = 0f;
                } else {
                    phi_581_ = 0u;
                    if (_e437 > 1f) {
                    } else {
                        phi_581_ = 1u;
                    }
                    let _e441 = phi_581_;
                    phi_583_ = _e437;
                    if (0u == _e441) {
                        phi_583_ = 1f;
                    }
                }
                let _e444 = phi_583_;
                let _e448 = ((_e444 * _e444) * (3f - (_e444 * 2f)));
                let _e455 = (1f - _e448);
                let _e462 = vec3<f32>(((((_e238 * 0.17f) + 0.17f) * _e448) + (((_e263 * 0.13f) + (_e426 * ((_e351 * 0.78f) + (_e410 * ((_e400 * (((_e288 * 0.05f) + 0.028f) + (_e364 * 0.034f))) + (_e318 * (((_e288 * 0.066f) + 0.119f) + (_e375 * 0.074f)))))))) * _e455)), vec3<f32>().y, vec3<f32>().z);
                let _e469 = vec3<f32>(_e462.x, ((((_e238 * 0.105f) + 0.23f) * _e448) + (((_e263 * 0.1f) + (_e426 * ((_e351 * 0.83f) + (_e410 * ((_e400 * (((_e288 * 0.086f) + 0.094f) - (_e364 * 0.05f))) + (_e318 * (((_e288 * 0.058f) + 0.112f) + (_e375 * 0.047f)))))))) * _e455)), _e462.z);
                phi_911_ = vec3<f32>(_e469.x, _e469.y, ((((_e238 * 0.052f) + 0.31f) * _e448) + (((_e263 * 0.07f) + (_e426 * ((_e351 * 0.87f) + (_e410 * ((_e400 * (((_e288 * 0.036f) + 0.043f) + (_e364 * 0.02f))) + (_e318 * (((_e288 * 0.054f) + 0.118f) + (_e375 * 0.037f)))))))) * _e455)));
                break;
            }
            default: {
                let _e492 = cos((((_e174.x * 1.37f) + (_e174.z * 0.91f)) + (_e182 * 1.12f)));
                let _e493 = cos((((_e174.z * 1.61f) - (_e174.x * 0.83f)) - (_e182 * 0.94f)));
                let _e494 = cos((((_e174.x * 2.11f) - (_e174.z * 0.57f)) + (_e182 * 0.71f)));
                let _e496 = ((_e175.w + -48f) / 48f);
                if (_e496 < 0f) {
                    phi_631_ = 0f;
                } else {
                    phi_629_ = 0u;
                    if (_e496 > 1f) {
                    } else {
                        phi_629_ = 1u;
                    }
                    let _e500 = phi_629_;
                    phi_631_ = _e496;
                    if (0u == _e500) {
                        phi_631_ = 1f;
                    }
                }
                let _e503 = phi_631_;
                let _e508 = (1f - ((_e503 * _e503) * (3f - (_e503 * 2f))));
                let _e515 = (_e249 - (((_e494 * 0.03195f) + ((_e492 * 0.07095f) - (_e493 * 0.02835f))) * _e508));
                let _e522 = (_e251 - ((((_e492 * 0.05115f) + (_e493 * 0.05355f)) - (_e494 * 0.00855f)) * _e508));
                let _e528 = sqrt(((_e522 * _e522) + ((_e250 * _e250) + (_e515 * _e515))));
                let _e529 = (_e515 / _e528);
                let _e530 = (_e250 / _e528);
                let _e531 = (_e522 / _e528);
                let _e542 = ((((sin((_e182 * 0.091f)) * 4.2f) + (cos((_e182 * 0.037f)) * 1.6f)) + 201.7f) - _e174.x);
                let _e543 = (12.8f - _e174.y);
                let _e544 = (((_e182 * 3.25f) + -92.1f) - _e174.z);
                let _e550 = sqrt(((_e544 * _e544) + ((_e543 * _e543) + (_e542 * _e542))));
                let _e551 = (_e542 / _e550);
                let _e552 = (_e543 / _e550);
                let _e553 = (_e544 / _e550);
                let _e558 = ((_e553 * _e531) + ((_e551 * _e529) + (_e552 * _e530)));
                if (_e558 < 0f) {
                    phi_691_ = 0f;
                } else {
                    phi_689_ = 0u;
                    if (_e558 > 1f) {
                    } else {
                        phi_689_ = 1u;
                    }
                    let _e562 = phi_689_;
                    phi_691_ = _e558;
                    if (0u == _e562) {
                        phi_691_ = 1f;
                    }
                }
                let _e565 = phi_691_;
                let _e566 = (1f - _e565);
                let _e567 = (_e566 * _e566);
                let _e571 = (((_e566 * (_e567 * _e567)) * 0.98f) + 0.02f);
                let _e574 = (((_e529 * _e565) * 2f) - _e551);
                let _e577 = (((_e530 * _e565) * 2f) - _e552);
                let _e580 = (((_e531 * _e565) * 2f) - _e553);
                let _e582 = ((_e577 * 0.72f) + 0.28f);
                if (_e582 < 0f) {
                    phi_713_ = 0f;
                } else {
                    phi_711_ = 0u;
                    if (_e582 > 1f) {
                    } else {
                        phi_711_ = 1u;
                    }
                    let _e586 = phi_711_;
                    phi_713_ = _e582;
                    if (0u == _e586) {
                        phi_713_ = 1f;
                    }
                }
                let _e589 = phi_713_;
                let _e590 = (1f - _e589);
                let _e618 = (((((sin(((_e182 * 0.018f) + ((_e574 * 2.1f) + (_e580 * 1.3f)))) * 0.5f) + 0.5f) + ((cos((((_e580 * 2.7f) - (_e574 * 1.4f)) - (_e182 * 0.013f))) * 0.5f) + 0.5f)) + -1.25f) / 0.47000003f);
                if (_e618 < 0f) {
                    phi_747_ = 0f;
                } else {
                    phi_745_ = 0u;
                    if (_e618 > 1f) {
                    } else {
                        phi_745_ = 1u;
                    }
                    let _e622 = phi_745_;
                    phi_747_ = _e618;
                    if (0u == _e622) {
                        phi_747_ = 1f;
                    }
                }
                let _e625 = phi_747_;
                let _e629 = ((_e625 * _e625) * (3f - (_e625 * 2f)));
                let _e640 = ((_e580 * 0.86f) + ((_e574 * 0.39f) + (_e577 * 0.32f)));
                if (_e640 < 0f) {
                    phi_767_ = 0f;
                } else {
                    phi_765_ = 0u;
                    if (_e640 > 1f) {
                    } else {
                        phi_765_ = 1u;
                    }
                    let _e644 = phi_765_;
                    phi_767_ = _e640;
                    if (0u == _e644) {
                        phi_767_ = 1f;
                    }
                }
                let _e647 = phi_767_;
                let _e648 = (_e647 * _e647);
                let _e649 = (_e648 * _e648);
                let _e650 = (_e649 * _e649);
                let _e651 = (_e650 * _e650);
                let _e652 = (_e651 * _e651);
                let _e656 = (((_e571 * 0.65f) + 0.35f) * (_e652 * _e652));
                let _e658 = ((_e176.z + -0.18f) / 3.62f);
                if (_e658 < 0f) {
                    phi_783_ = 0f;
                } else {
                    phi_781_ = 0u;
                    if (_e658 > 1f) {
                    } else {
                        phi_781_ = 1u;
                    }
                    let _e662 = phi_781_;
                    phi_783_ = _e658;
                    if (0u == _e662) {
                        phi_783_ = 1f;
                    }
                }
                let _e665 = phi_783_;
                let _e671 = (_e508 * (1f - ((_e665 * _e665) * (3f - (_e665 * 2f)))));
                let _e673 = ((_e175.w + -24f) / 196f);
                if (_e673 < 0f) {
                    phi_796_ = 0f;
                } else {
                    phi_794_ = 0u;
                    if (_e673 > 1f) {
                    } else {
                        phi_794_ = 1u;
                    }
                    let _e677 = phi_794_;
                    phi_796_ = _e673;
                    if (0u == _e677) {
                        phi_796_ = 1f;
                    }
                }
                let _e680 = phi_796_;
                let _e684 = ((_e680 * _e680) * (3f - (_e680 * 2f)));
                let _e700 = (1f - _e671);
                let _e717 = ((_e176.z + -0.05f) / 0.59999996f);
                if (_e717 < 0f) {
                    phi_838_ = 0f;
                } else {
                    phi_836_ = 0u;
                    if (_e717 > 1f) {
                    } else {
                        phi_836_ = 1u;
                    }
                    let _e721 = phi_836_;
                    phi_838_ = _e717;
                    if (0u == _e721) {
                        phi_838_ = 1f;
                    }
                }
                let _e724 = phi_838_;
                let _e736 = (abs(((_e494 * 0.28f) + (_e492 + (_e493 * 0.72f)))) / 0.18f);
                let _e738 = select(_e736, 1f, (_e736 > 1f));
                let _e744 = ((1f - ((_e738 * _e738) * (3f - (_e738 * 2f)))) * (_e508 * (1f - ((_e724 * _e724) * (3f - (_e724 * 2f))))));
                let _e751 = (1f - _e571);
                let _e774 = ((_e175.w + -232f) / 144f);
                if (_e774 < 0f) {
                    phi_893_ = 0f;
                } else {
                    phi_891_ = 0u;
                    if (_e774 > 1f) {
                    } else {
                        phi_891_ = 1u;
                    }
                    let _e778 = phi_891_;
                    phi_893_ = _e774;
                    if (0u == _e778) {
                        phi_893_ = 1f;
                    }
                }
                let _e781 = phi_893_;
                let _e785 = ((_e781 * _e781) * (3f - (_e781 * 2f)));
                let _e786 = (1f - _e785);
                let _e793 = vec3<f32>(((_e785 * 0.17f) + (((_e656 * 0.7f) + ((_e652 * 0.08f) + ((_e571 * (((_e589 * 0.025f) + (_e590 * 0.16f)) + (_e629 * 0.028f))) + (_e751 * (((((_e176.y * 0.012f) + 0.018f) * _e671) + (_e700 * (((_e176.x * 0.008f) + 0.004f) * (1f - (_e684 * 0.42f))))) + (_e744 * 0.006f)))))) * _e786)), vec3<f32>().y, vec3<f32>().z);
                let _e800 = vec3<f32>(_e793.x, ((_e785 * 0.23f) + (((_e656 * 0.42f) + ((_e652 * 0.045f) + ((_e571 * (((_e589 * 0.095f) + (_e590 * 0.24f)) + (_e629 * 0.036f))) + (_e751 * (((((_e176.y * 0.05f) + 0.105f) * _e671) + (_e700 * (((_e176.x * 0.038f) + 0.018f) * (1f - (_e684 * 0.28f))))) + (_e744 * 0.032f)))))) * _e786)), _e793.z);
                phi_911_ = vec3<f32>(_e800.x, _e800.y, ((_e785 * 0.31f) + (((_e656 * 0.16f) + ((_e652 * 0.018f) + ((_e571 * (((_e589 * 0.24f) + (_e590 * 0.32f)) + (_e629 * 0.045f))) + (_e751 * (((((_e176.y * 0.055f) + 0.125f) * _e671) + (_e700 * (((_e176.x * 0.058f) + 0.042f) * (1f - (_e684 * 0.12f))))) + (_e744 * 0.04f)))))) * _e786)));
                break;
            }
        }
        let _e809 = phi_911_;
        phi_996_ = _e809;
        let _e910 = phi_996_;
        let _e911 = (_e193 / _e189);
        let _e917 = ((sqrt(((_e196 * _e196) + (_e911 * _e911))) + -0.58f) / 0.76000005f);
        local = (_e917 < 0f);
        local_1 = _e917;
        local_5 = _e917;
        local_2 = _e910;
        local_3 = _e910;
        local_4 = _e910;
    }
    let _e920 = local;
    if _e920 {
        phi_1008_ = 0f;
    } else {
        let _e922 = local_1;
        phi_1006_ = 0u;
        if (_e922 > 1f) {
        } else {
            phi_1006_ = 1u;
        }
        let _e925 = phi_1006_;
        let _e1031 = local_5;
        phi_1008_ = _e1031;
        if (0u == _e925) {
            phi_1008_ = 1f;
        }
    }
    let _e928 = phi_1008_;
    let _e935 = (((1f - ((_e928 * _e928) * (3f - (_e928 * 2f)))) * 0.62f) + 0.38f);
    let _e937 = local_2;
    let _e944 = local_3;
    let _e951 = local_4;
    let _e961 = vec4<f32>(((_e937.x * _e935) / ((_e937.x * 0.38f) + 1f)), vec4<f32>(0f, 0f, 0f, 1f).y, vec4<f32>(0f, 0f, 0f, 1f).z, vec4<f32>(0f, 0f, 0f, 1f).w);
    let _e966 = vec4<f32>(_e961.x, ((_e944.y * _e935) / ((_e944.y * 0.38f) + 1f)), _e961.z, _e961.w);
    gl_FragColor = vec4<f32>(_e966.x, _e966.y, ((_e951.z * _e935) / ((_e951.z * 0.38f) + 1f)), _e966.w);
    return;
}

@fragment 
fn main(@builtin(position) gl_FragCoord: vec4<f32>, @location(2) dynlex_interpolant_7465727261696e20706f736974696f6e: vec4<f32>, @location(1) dynlex_interpolant_7465727261696e206e6f726d616c: vec4<f32>, @location(0) dynlex_interpolant_7465727261696e206d6174657269616c: vec4<f32>) -> @location(0) vec4<f32> {
    gl_FragCoord_1 = gl_FragCoord;
    dynlex_interpolant_7465727261696e20706f736974696f6e_1 = dynlex_interpolant_7465727261696e20706f736974696f6e;
    dynlex_interpolant_7465727261696e206e6f726d616c_1 = dynlex_interpolant_7465727261696e206e6f726d616c;
    dynlex_interpolant_7465727261696e206d6174657269616c_1 = dynlex_interpolant_7465727261696e206d6174657269616c;
    main_1();
    let _e9 = gl_FragColor;
    return _e9;
}
