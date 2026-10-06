if (kind == IntrinsicKind::ShaderInterpolantInput) {
	expr->type = {DataType::Kind::Vector};
	expr->type.arraySize = 4;
	expr->type.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
	break;
}
