struct type_7 {
    member: f32,
}

var<private> gl_FragCoord_1: vec4<f32>;
var<private> gl_FragColor: vec4<f32>;
@group(0) @binding(0) 
var<uniform> ubo_time: type_7;
@group(0) @binding(2) 
var<uniform> ubo_width: type_7;
@group(0) @binding(3) 
var<uniform> ubo_height: type_7;
var<private> dynlex_interpolant_7465727261696e20706f736974696f6e_1: vec4<f32>;
var<private> dynlex_interpolant_7465727261696e206e6f726d616c_1: vec4<f32>;
var<private> dynlex_interpolant_7465727261696e206d6174657269616c_1: vec4<f32>;

fn main_1() {
    var phi_392_: u32;
    var phi_394_: f32;
    var phi_404_: u32;
    var phi_406_: f32;
    var phi_432_: u32;
    var phi_434_: f32;
    var phi_439_: u32;
    var phi_441_: f32;
    var phi_452_: u32;
    var phi_453_: f32;
    var phi_454_: f32;
    var phi_455_: f32;
    var phi_456_: u32;
    var phi_458_: u32;
    var phi_460_: f32;
    var phi_461_: f32;
    var phi_469_: u32;
    var phi_471_: f32;
    var phi_483_: u32;
    var phi_485_: f32;
    var phi_494_: u32;
    var phi_496_: f32;
    var phi_509_: u32;
    var phi_511_: f32;
    var phi_522_: u32;
    var phi_524_: f32;
    var phi_589_: u32;
    var phi_591_: f32;
    var phi_637_: u32;
    var phi_639_: f32;
    var phi_697_: u32;
    var phi_699_: f32;
    var phi_719_: u32;
    var phi_721_: f32;
    var phi_753_: u32;
    var phi_755_: f32;
    var phi_773_: u32;
    var phi_775_: f32;
    var phi_789_: u32;
    var phi_791_: f32;
    var phi_802_: u32;
    var phi_804_: f32;
    var phi_844_: u32;
    var phi_846_: f32;
    var phi_899_: u32;
    var phi_901_: f32;
    var phi_919_: vec3<f32>;
    var phi_924_: u32;
    var phi_926_: f32;
    var phi_964_: u32;
    var phi_966_: f32;
    var phi_975_: u32;
    var phi_977_: f32;
    var phi_1004_: vec3<f32>;
    var local: bool;
    var local_1: f32;
    var phi_1014_: u32;
    var phi_1016_: f32;
    var local_2: vec3<f32>;
    var local_3: vec3<f32>;
    var local_4: vec3<f32>;
    var local_5: f32;

    let _e174 = gl_FragCoord_1;
    let _e176 = gl_FragCoord_1;
    let _e179 = ubo_time.member;
    let _e181 = ubo_width.member;
    let _e182 = max(_e181, 1f);
    let _e184 = ubo_height.member;
    let _e185 = max(_e184, 1f);
    let _e186 = (_e182 / _e185);
    let _e190 = (_e186 * (((_e174.x / _e182) * 2f) + -1f));
    let _e193 = (1f - ((_e176.y / _e185) * 2f));
    let _e194 = dynlex_interpolant_7465727261696e20706f736974696f6e_1;
    let _e196 = dynlex_interpolant_7465727261696e20706f736974696f6e_1;
    let _e198 = dynlex_interpolant_7465727261696e20706f736974696f6e_1;
    let _e200 = dynlex_interpolant_7465727261696e20706f736974696f6e_1;
    let _e202 = dynlex_interpolant_7465727261696e206e6f726d616c_1;
    let _e203 = dynlex_interpolant_7465727261696e206e6f726d616c_1;
    let _e204 = dynlex_interpolant_7465727261696e206e6f726d616c_1;
    let _e205 = dynlex_interpolant_7465727261696e206e6f726d616c_1;
    let _e207 = dynlex_interpolant_7465727261696e206d6174657269616c_1;
    let _e209 = dynlex_interpolant_7465727261696e206d6174657269616c_1;
    let _e211 = dynlex_interpolant_7465727261696e206d6174657269616c_1;
    let _e213 = (_e190 + -0.52f);
    let _e214 = (_e193 + -0.36f);
    let _e218 = sqrt(((_e213 * _e213) + (_e214 * _e214)));
    let _e220 = ((_e218 + -0.014f) / 0.051f);
    if (_e220 < 0f) {
        phi_394_ = 0f;
    } else {
        phi_392_ = 0u;
        if (_e220 > 1f) {
        } else {
            phi_392_ = 1u;
        }
        let _e224 = phi_392_;
        phi_394_ = _e220;
        if (0u == _e224) {
            phi_394_ = 1f;
        }
    }
    let _e227 = phi_394_;
    let _e232 = (1f - ((_e227 * _e227) * (3f - (_e227 * 2f))));
    let _e234 = ((_e218 + -0.04f) / 0.4f);
    if (_e234 < 0f) {
        phi_406_ = 0f;
    } else {
        phi_404_ = 0u;
        if (_e234 > 1f) {
        } else {
            phi_404_ = 1u;
        }
        let _e238 = phi_404_;
        phi_406_ = _e234;
        if (0u == _e238) {
            phi_406_ = 1f;
        }
    }
    let _e241 = phi_406_;
    let _e246 = (1f - ((_e241 * _e241) * (3f - (_e241 * 2f))));
    if (_e200.w < 0.5f) {
        let _e819 = ((_e193 * 0.48f) + 0.43f);
        if (_e819 < 0f) {
            phi_926_ = 0f;
        } else {
            phi_924_ = 0u;
            if (_e819 > 1f) {
            } else {
                phi_924_ = 1u;
            }
            let _e823 = phi_924_;
            phi_926_ = _e819;
            if (0u == _e823) {
                phi_926_ = 1f;
            }
        }
        let _e826 = phi_926_;
        let _e840 = ((sin(((_e179 * 0.021f) + ((_e190 * 1.21f) + (_e193 * 1.77f)))) * 0.5f) + 0.5f);
        let _e861 = ((((_e840 + ((cos((((_e193 * 2.93f) - (_e190 * 0.67f)) - (_e179 * 0.016f))) * 0.5f) + 0.5f)) + (((sin((((_e190 * 2.47f) - (_e193 * 4.11f)) + (_e840 * 1.8f))) * 0.5f) + 0.5f) * 0.46f)) + -1.42f) / 0.7600001f);
        if (_e861 < 0f) {
            phi_966_ = 0f;
        } else {
            phi_964_ = 0u;
            if (_e861 > 1f) {
            } else {
                phi_964_ = 1u;
            }
            let _e865 = phi_964_;
            phi_966_ = _e861;
            if (0u == _e865) {
                phi_966_ = 1f;
            }
        }
        let _e868 = phi_966_;
        let _e874 = ((_e193 + 0.08f) / 0.45999998f);
        if (_e874 < 0f) {
            phi_977_ = 0f;
        } else {
            phi_975_ = 0u;
            if (_e874 > 1f) {
            } else {
                phi_975_ = 1u;
            }
            let _e878 = phi_975_;
            phi_977_ = _e874;
            if (0u == _e878) {
                phi_977_ = 1f;
            }
        }
        let _e881 = phi_977_;
        let _e886 = (((_e868 * _e868) * (3f - (_e868 * 2f))) * ((_e881 * _e881) * (3f - (_e881 * 2f))));
        let _e900 = vec3<f32>(((_e232 * 1.36f) + ((_e246 * 0.31f) + (((_e826 * 0.105f) + 0.022f) + (_e886 * 0.33f)))), vec3<f32>().y, vec3<f32>().z);
        let _e908 = vec3<f32>(_e900.x, ((_e232 * 0.96f) + ((_e246 * 0.19f) + (((_e826 * 0.18f) + 0.061f) + (_e886 * 0.31f)))), _e900.z);
        phi_1004_ = vec3<f32>(_e908.x, _e908.y, ((_e232 * 0.55f) + ((_e246 * 0.09f) + (((_e826 * 0.33f) + 0.14f) + (_e886 * 0.29f)))));
    } else {
        let _e256 = sqrt((((_e202.x * _e202.x) + (_e203.y * _e203.y)) + (_e204.z * _e204.z)));
        let _e257 = (_e202.x / _e256);
        let _e258 = (_e203.y / _e256);
        let _e259 = (_e204.z / _e256);
        let _e264 = (((_e258 * 0.79f) - (_e257 * 0.43f)) - (_e259 * 0.44f));
        if (_e264 < 0f) {
            phi_434_ = 0f;
        } else {
            phi_432_ = 0u;
            if (_e264 > 1f) {
            } else {
                phi_432_ = 1u;
            }
            let _e268 = phi_432_;
            phi_434_ = _e264;
            if (0u == _e268) {
                phi_434_ = 1f;
            }
        }
        let _e271 = phi_434_;
        let _e273 = ((_e258 * 0.56f) + 0.44f);
        if (_e273 < 0f) {
            phi_441_ = 0f;
        } else {
            phi_439_ = 0u;
            if (_e273 > 1f) {
            } else {
                phi_439_ = 1u;
            }
            let _e277 = phi_439_;
            phi_441_ = _e273;
            if (0u == _e277) {
                phi_441_ = 1f;
            }
        }
        let _e280 = phi_441_;
        phi_453_ = f32();
        phi_454_ = f32();
        phi_455_ = f32();
        phi_456_ = 0u;
        if (_e200.w > 1.5f) {
        } else {
            let _e282 = (1f - _e258);
            let _e289 = (((_e209.y + -0.32f) * 1.35f) + (_e282 * 1.7f));
            phi_452_ = 1u;
            if (_e289 < 0f) {
            } else {
                phi_452_ = 2u;
            }
            let _e292 = phi_452_;
            phi_453_ = _e289;
            phi_454_ = ((_e207.x * 0.68f) + (_e209.y * 0.32f));
            phi_455_ = _e282;
            phi_456_ = _e292;
        }
        let _e294 = phi_453_;
        let _e296 = phi_454_;
        let _e298 = phi_455_;
        let _e300 = phi_456_;
        switch bitcast<i32>(_e300) {
            case 2, 1: {
                switch bitcast<i32>(_e300) {
                    case 1: {
                        phi_461_ = 0f;
                        break;
                    }
                    case 2: {
                        phi_458_ = 0u;
                        if (_e294 > 1f) {
                        } else {
                            phi_458_ = 1u;
                        }
                        let _e305 = phi_458_;
                        phi_460_ = _e294;
                        if (0u == _e305) {
                            phi_460_ = 1f;
                        }
                        let _e308 = phi_460_;
                        phi_461_ = _e308;
                        break;
                    }
                    default: {
                    }
                }
                let _e310 = phi_461_;
                let _e315 = (((_e298 + ((_e310 + -0.5f) * 0.095f)) + -0.055f) / 0.245f);
                if (_e315 < 0f) {
                    phi_471_ = 0f;
                } else {
                    phi_469_ = 0u;
                    if (_e315 > 1f) {
                    } else {
                        phi_469_ = 1u;
                    }
                    let _e319 = phi_469_;
                    phi_471_ = _e315;
                    if (0u == _e319) {
                        phi_471_ = 1f;
                    }
                }
                let _e322 = phi_471_;
                let _e326 = ((_e322 * _e322) * (3f - (_e322 * 2f)));
                let _e331 = (((_e196.y + ((_e296 + -0.5f) * 0.48f)) + -2.75f) / 1.6999998f);
                if (_e331 < 0f) {
                    phi_485_ = 0f;
                } else {
                    phi_483_ = 0u;
                    if (_e331 > 1f) {
                    } else {
                        phi_483_ = 1u;
                    }
                    let _e335 = phi_483_;
                    phi_485_ = _e331;
                    if (0u == _e335) {
                        phi_485_ = 1f;
                    }
                }
                let _e338 = phi_485_;
                let _e344 = ((_e258 + -0.25f) / 0.41000003f);
                if (_e344 < 0f) {
                    phi_496_ = 0f;
                } else {
                    phi_494_ = 0u;
                    if (_e344 > 1f) {
                    } else {
                        phi_494_ = 1u;
                    }
                    let _e348 = phi_494_;
                    phi_496_ = _e344;
                    if (0u == _e348) {
                        phi_496_ = 1f;
                    }
                }
                let _e351 = phi_496_;
                let _e359 = (((_e209.y * 0.12f) + 0.88f) * (((_e338 * _e338) * (3f - (_e338 * 2f))) * ((_e351 * _e351) * (3f - (_e351 * 2f)))));
                let _e361 = ((_e196.y + -0.35f) / 2.3000002f);
                if (_e361 < 0f) {
                    phi_511_ = 0f;
                } else {
                    phi_509_ = 0u;
                    if (_e361 > 1f) {
                    } else {
                        phi_509_ = 1u;
                    }
                    let _e365 = phi_509_;
                    phi_511_ = _e361;
                    if (0u == _e365) {
                        phi_511_ = 1f;
                    }
                }
                let _e368 = phi_511_;
                let _e372 = ((_e368 * _e368) * (3f - (_e368 * 2f)));
                let _e376 = (((_e257 * 0.38f) - (_e259 * 0.26f)) + 0.48f);
                if (_e376 < 0f) {
                    phi_524_ = 0f;
                } else {
                    phi_522_ = 0u;
                    if (_e376 > 1f) {
                    } else {
                        phi_522_ = 1u;
                    }
                    let _e380 = phi_522_;
                    phi_524_ = _e376;
                    if (0u == _e380) {
                        phi_524_ = 1f;
                    }
                }
                let _e383 = phi_524_;
                let _e408 = (1f - _e326);
                let _e418 = (1f - _e359);
                let _e434 = (((_e209.y * 0.26f) + 0.8f) * (((_e271 * 1.16f) + 0.17f) + (_e280 * 0.17f)));
                let _e445 = ((_e205.w + -188f) / 188f);
                if (_e445 < 0f) {
                    phi_591_ = 0f;
                } else {
                    phi_589_ = 0u;
                    if (_e445 > 1f) {
                    } else {
                        phi_589_ = 1u;
                    }
                    let _e449 = phi_589_;
                    phi_591_ = _e445;
                    if (0u == _e449) {
                        phi_591_ = 1f;
                    }
                }
                let _e452 = phi_591_;
                let _e456 = ((_e452 * _e452) * (3f - (_e452 * 2f)));
                let _e463 = (1f - _e456);
                let _e470 = vec3<f32>(((((_e246 * 0.17f) + 0.17f) * _e456) + (((_e271 * 0.13f) + (_e434 * ((_e359 * 0.78f) + (_e418 * ((_e408 * (((_e296 * 0.05f) + 0.028f) + (_e372 * 0.034f))) + (_e326 * (((_e296 * 0.066f) + 0.119f) + (_e383 * 0.074f)))))))) * _e463)), vec3<f32>().y, vec3<f32>().z);
                let _e477 = vec3<f32>(_e470.x, ((((_e246 * 0.105f) + 0.23f) * _e456) + (((_e271 * 0.1f) + (_e434 * ((_e359 * 0.83f) + (_e418 * ((_e408 * (((_e296 * 0.086f) + 0.094f) - (_e372 * 0.05f))) + (_e326 * (((_e296 * 0.058f) + 0.112f) + (_e383 * 0.047f)))))))) * _e463)), _e470.z);
                phi_919_ = vec3<f32>(_e477.x, _e477.y, ((((_e246 * 0.052f) + 0.31f) * _e456) + (((_e271 * 0.07f) + (_e434 * ((_e359 * 0.87f) + (_e418 * ((_e408 * (((_e296 * 0.036f) + 0.043f) + (_e372 * 0.02f))) + (_e326 * (((_e296 * 0.054f) + 0.118f) + (_e383 * 0.037f)))))))) * _e463)));
                break;
            }
            default: {
                let _e500 = cos(((_e179 * 1.12f) + ((_e194.x * 1.37f) + (_e198.z * 0.91f))));
                let _e501 = cos((((_e198.z * 1.61f) - (_e194.x * 0.83f)) - (_e179 * 0.94f)));
                let _e502 = cos(((_e179 * 0.71f) + ((_e194.x * 2.11f) - (_e198.z * 0.57f))));
                let _e504 = ((_e205.w + -48f) / 48f);
                if (_e504 < 0f) {
                    phi_639_ = 0f;
                } else {
                    phi_637_ = 0u;
                    if (_e504 > 1f) {
                    } else {
                        phi_637_ = 1u;
                    }
                    let _e508 = phi_637_;
                    phi_639_ = _e504;
                    if (0u == _e508) {
                        phi_639_ = 1f;
                    }
                }
                let _e511 = phi_639_;
                let _e516 = (1f - ((_e511 * _e511) * (3f - (_e511 * 2f))));
                let _e523 = (_e257 - (((_e502 * 0.03195f) + ((_e500 * 0.07095f) - (_e501 * 0.02835f))) * _e516));
                let _e530 = (_e259 - ((((_e500 * 0.05115f) + (_e501 * 0.05355f)) - (_e502 * 0.00855f)) * _e516));
                let _e536 = sqrt(((_e530 * _e530) + ((_e258 * _e258) + (_e523 * _e523))));
                let _e537 = (_e523 / _e536);
                let _e538 = (_e258 / _e536);
                let _e539 = (_e530 / _e536);
                let _e550 = ((((sin((_e179 * 0.091f)) * 4.2f) + (cos((_e179 * 0.037f)) * 1.6f)) + 201.7f) - _e194.x);
                let _e551 = (12.8f - _e196.y);
                let _e552 = (((_e179 * 3.25f) + -92.1f) - _e198.z);
                let _e558 = sqrt((((_e550 * _e550) + (_e551 * _e551)) + (_e552 * _e552)));
                let _e559 = (_e550 / _e558);
                let _e560 = (_e551 / _e558);
                let _e561 = (_e552 / _e558);
                let _e566 = ((_e561 * _e539) + ((_e559 * _e537) + (_e560 * _e538)));
                if (_e566 < 0f) {
                    phi_699_ = 0f;
                } else {
                    phi_697_ = 0u;
                    if (_e566 > 1f) {
                    } else {
                        phi_697_ = 1u;
                    }
                    let _e570 = phi_697_;
                    phi_699_ = _e566;
                    if (0u == _e570) {
                        phi_699_ = 1f;
                    }
                }
                let _e573 = phi_699_;
                let _e574 = (1f - _e573);
                let _e575 = (_e574 * _e574);
                let _e579 = (((_e574 * (_e575 * _e575)) * 0.98f) + 0.02f);
                let _e582 = (((_e537 * _e573) * 2f) - _e559);
                let _e585 = (((_e538 * _e573) * 2f) - _e560);
                let _e588 = (((_e539 * _e573) * 2f) - _e561);
                let _e590 = ((_e585 * 0.72f) + 0.28f);
                if (_e590 < 0f) {
                    phi_721_ = 0f;
                } else {
                    phi_719_ = 0u;
                    if (_e590 > 1f) {
                    } else {
                        phi_719_ = 1u;
                    }
                    let _e594 = phi_719_;
                    phi_721_ = _e590;
                    if (0u == _e594) {
                        phi_721_ = 1f;
                    }
                }
                let _e597 = phi_721_;
                let _e598 = (1f - _e597);
                let _e626 = (((((sin(((_e179 * 0.018f) + ((_e582 * 2.1f) + (_e588 * 1.3f)))) * 0.5f) + 0.5f) + ((cos((((_e588 * 2.7f) - (_e582 * 1.4f)) - (_e179 * 0.013f))) * 0.5f) + 0.5f)) + -1.25f) / 0.47000003f);
                if (_e626 < 0f) {
                    phi_755_ = 0f;
                } else {
                    phi_753_ = 0u;
                    if (_e626 > 1f) {
                    } else {
                        phi_753_ = 1u;
                    }
                    let _e630 = phi_753_;
                    phi_755_ = _e626;
                    if (0u == _e630) {
                        phi_755_ = 1f;
                    }
                }
                let _e633 = phi_755_;
                let _e637 = ((_e633 * _e633) * (3f - (_e633 * 2f)));
                let _e648 = ((_e588 * 0.86f) + ((_e582 * 0.39f) + (_e585 * 0.32f)));
                if (_e648 < 0f) {
                    phi_775_ = 0f;
                } else {
                    phi_773_ = 0u;
                    if (_e648 > 1f) {
                    } else {
                        phi_773_ = 1u;
                    }
                    let _e652 = phi_773_;
                    phi_775_ = _e648;
                    if (0u == _e652) {
                        phi_775_ = 1f;
                    }
                }
                let _e655 = phi_775_;
                let _e656 = (_e655 * _e655);
                let _e657 = (_e656 * _e656);
                let _e658 = (_e657 * _e657);
                let _e659 = (_e658 * _e658);
                let _e660 = (_e659 * _e659);
                let _e664 = (((_e579 * 0.65f) + 0.35f) * (_e660 * _e660));
                let _e666 = ((_e211.z + -0.18f) / 3.62f);
                if (_e666 < 0f) {
                    phi_791_ = 0f;
                } else {
                    phi_789_ = 0u;
                    if (_e666 > 1f) {
                    } else {
                        phi_789_ = 1u;
                    }
                    let _e670 = phi_789_;
                    phi_791_ = _e666;
                    if (0u == _e670) {
                        phi_791_ = 1f;
                    }
                }
                let _e673 = phi_791_;
                let _e679 = (_e516 * (1f - ((_e673 * _e673) * (3f - (_e673 * 2f)))));
                let _e681 = ((_e205.w + -24f) / 196f);
                if (_e681 < 0f) {
                    phi_804_ = 0f;
                } else {
                    phi_802_ = 0u;
                    if (_e681 > 1f) {
                    } else {
                        phi_802_ = 1u;
                    }
                    let _e685 = phi_802_;
                    phi_804_ = _e681;
                    if (0u == _e685) {
                        phi_804_ = 1f;
                    }
                }
                let _e688 = phi_804_;
                let _e692 = ((_e688 * _e688) * (3f - (_e688 * 2f)));
                let _e708 = (1f - _e679);
                let _e725 = ((_e211.z + -0.05f) / 0.59999996f);
                if (_e725 < 0f) {
                    phi_846_ = 0f;
                } else {
                    phi_844_ = 0u;
                    if (_e725 > 1f) {
                    } else {
                        phi_844_ = 1u;
                    }
                    let _e729 = phi_844_;
                    phi_846_ = _e725;
                    if (0u == _e729) {
                        phi_846_ = 1f;
                    }
                }
                let _e732 = phi_846_;
                let _e744 = (abs(((_e502 * 0.28f) + (_e500 + (_e501 * 0.72f)))) / 0.18f);
                let _e746 = select(_e744, 1f, (_e744 > 1f));
                let _e752 = ((1f - ((_e746 * _e746) * (3f - (_e746 * 2f)))) * (_e516 * (1f - ((_e732 * _e732) * (3f - (_e732 * 2f))))));
                let _e759 = (1f - _e579);
                let _e782 = ((_e205.w + -232f) / 144f);
                if (_e782 < 0f) {
                    phi_901_ = 0f;
                } else {
                    phi_899_ = 0u;
                    if (_e782 > 1f) {
                    } else {
                        phi_899_ = 1u;
                    }
                    let _e786 = phi_899_;
                    phi_901_ = _e782;
                    if (0u == _e786) {
                        phi_901_ = 1f;
                    }
                }
                let _e789 = phi_901_;
                let _e793 = ((_e789 * _e789) * (3f - (_e789 * 2f)));
                let _e794 = (1f - _e793);
                let _e801 = vec3<f32>(((_e793 * 0.17f) + (((_e664 * 0.7f) + ((_e660 * 0.08f) + ((_e579 * (((_e597 * 0.025f) + (_e598 * 0.16f)) + (_e637 * 0.028f))) + (_e759 * (((((_e209.y * 0.012f) + 0.018f) * _e679) + (_e708 * (((_e207.x * 0.008f) + 0.004f) * (1f - (_e692 * 0.42f))))) + (_e752 * 0.006f)))))) * _e794)), vec3<f32>().y, vec3<f32>().z);
                let _e808 = vec3<f32>(_e801.x, ((_e793 * 0.23f) + (((_e664 * 0.42f) + ((_e660 * 0.045f) + ((_e579 * (((_e597 * 0.095f) + (_e598 * 0.24f)) + (_e637 * 0.036f))) + (_e759 * (((((_e209.y * 0.05f) + 0.105f) * _e679) + (_e708 * (((_e207.x * 0.038f) + 0.018f) * (1f - (_e692 * 0.28f))))) + (_e752 * 0.032f)))))) * _e794)), _e801.z);
                phi_919_ = vec3<f32>(_e808.x, _e808.y, ((_e793 * 0.31f) + (((_e664 * 0.16f) + ((_e660 * 0.018f) + ((_e579 * (((_e597 * 0.24f) + (_e598 * 0.32f)) + (_e637 * 0.045f))) + (_e759 * (((((_e209.y * 0.055f) + 0.125f) * _e679) + (_e708 * (((_e207.x * 0.058f) + 0.042f) * (1f - (_e692 * 0.12f))))) + (_e752 * 0.04f)))))) * _e794)));
                break;
            }
        }
        let _e817 = phi_919_;
        phi_1004_ = _e817;
        let _e918 = phi_1004_;
        let _e919 = (_e190 / _e186);
        let _e925 = ((sqrt(((_e193 * _e193) + (_e919 * _e919))) + -0.58f) / 0.76000005f);
        local = (_e925 < 0f);
        local_1 = _e925;
        local_5 = _e925;
        local_2 = _e918;
        local_3 = _e918;
        local_4 = _e918;
    }
    let _e928 = local;
    if _e928 {
        phi_1016_ = 0f;
    } else {
        let _e930 = local_1;
        phi_1014_ = 0u;
        if (_e930 > 1f) {
        } else {
            phi_1014_ = 1u;
        }
        let _e933 = phi_1014_;
        let _e1039 = local_5;
        phi_1016_ = _e1039;
        if (0u == _e933) {
            phi_1016_ = 1f;
        }
    }
    let _e936 = phi_1016_;
    let _e943 = (((1f - ((_e936 * _e936) * (3f - (_e936 * 2f)))) * 0.62f) + 0.38f);
    let _e945 = local_2;
    let _e952 = local_3;
    let _e959 = local_4;
    let _e969 = vec4<f32>(((_e945.x * _e943) / ((_e945.x * 0.38f) + 1f)), vec4<f32>(0f, 0f, 0f, 1f).y, vec4<f32>(0f, 0f, 0f, 1f).z, vec4<f32>(0f, 0f, 0f, 1f).w);
    let _e974 = vec4<f32>(_e969.x, ((_e952.y * _e943) / ((_e952.y * 0.38f) + 1f)), _e969.z, _e969.w);
    gl_FragColor = vec4<f32>(_e974.x, _e974.y, ((_e959.z * _e943) / ((_e959.z * 0.38f) + 1f)), _e974.w);
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
