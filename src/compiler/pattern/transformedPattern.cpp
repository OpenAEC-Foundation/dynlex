#include "transformedPattern.h"
#include "patternReference.h"

TransformedPattern::TransformedPattern(std::string pattern) : text(pattern) {}

size_t TransformedPattern::transformPosition(
	size_t position, KeyFrameTransformer keyFrameToInpos, KeyFrameTransformer keyFrameToOutPos
) {
	for (KeyFrame frame : keyframes | std::views::reverse) {
		size_t inPos = keyFrameToInpos(frame);
		if (inPos <= position) {
			return keyFrameToOutPos(frame) + position - inPos;
		}
	}
	return position;
}

size_t TransformedPattern::getLinePos(size_t patternPos) {
	return transformPosition(patternPos, [](const KeyFrame &frame) {
		return frame.patternPos;
	}, [](const KeyFrame &frame) {
		return frame.linePos;
	});
}

size_t TransformedPattern::getPatternPos(size_t linePos) {
	return transformPosition(linePos, [](const KeyFrame &frame) {
		return frame.linePos;
	}, [](const KeyFrame &frame) {
		return frame.patternPos;
	});
}

void TransformedPattern::replaceLine(size_t lineStartPos, size_t lineEndPos, std::string replacement) {
	replaceLocal(getPatternPos(lineStartPos), getPatternPos(lineEndPos), lineEndPos, replacement);
}

void TransformedPattern::replacePattern(size_t patternStartPos, size_t patternEndPos, std::string replacement) {
	replaceLocal(patternStartPos, patternEndPos, getLinePos(patternEndPos), replacement);
}
std::string TransformedPattern::toString() const {
	std::string result;
	for (char c : text) {
		if (c == argumentChar) {
			result += "$";
		} else {
			result += c;
		}
	}
	return result;
}

void TransformedPattern::replaceLocal(
	size_t patternStartPos, size_t patternEndPos, size_t lineEndPos, std::string replacement
) {
	text = text.substr(0, patternStartPos) + replacement + text.substr(patternEndPos);
	// insert a new keyframe after the argument char and check for any keyframes which got redundant
	int shift = (patternEndPos - patternStartPos) + replacement.length();
	if (shift) {
		bool added{};
		const KeyFrame &newKeyFrame{.patternPos = patternStartPos + replacement.length(), .linePos = lineEndPos};
		for (auto it = keyframes.begin(); it != keyframes.end();) {
			KeyFrame &keyFrame = *it;
			if (keyFrame.patternPos > patternStartPos) {
				if (keyFrame.patternPos >= patternEndPos) {
					if (!added) {
						added = true;
						// insert the new keyframe behind this keyframe
						it = keyframes.insert(it, newKeyFrame);
						// go past the new keyframe so the other ++ will go past the old keyframe
						it++;
					}
					// shift all keyframes on the right
					keyFrame.patternPos -= shift;
				} else {
					// remove this keyframe, it got replaced
					it = keyframes.erase(it);
					continue;
				}
			}
			it++;
		}
		if (!added) {
			// at the back
			keyframes.push_back(newKeyFrame);
		}
	}
}
