#include "patternReference.h"

TransformedPattern::TransformedPattern(std::string pattern) : text(pattern)
{
}

inline int TransformedPattern::transformPosition(int position, keyFrameTransformer keyFrameToInpos, keyFrameTransformer keyFrameToOutPos)
{
	for (KeyFrame frame : keyframes | std::views::reverse)
	{
		int inPos = keyFrameToInpos(frame);
		if (inPos < position)
		{
			return keyFrameToOutPos(frame) + position - inPos;
		}
	}
	return position;
}

inline int TransformedPattern::getLinePos(int patternPos)
{
	return transformPosition(patternPos, [](const KeyFrame &frame)
							 { return frame.patternPos; }, [](const KeyFrame &frame)
							 { return frame.linePos; });
}

inline int TransformedPattern::getPatternPos(int linePos)
{
	return transformPosition(linePos, [](const KeyFrame &frame)
							 { return frame.linePos; }, [](const KeyFrame &frame)
							 { return frame.patternPos; });
}

void TransformedPattern::replace(int lineStartPos, int lineEndPos)
{
	replaceLocal(getPatternPos(lineStartPos), getPatternPos(lineEndPos), lineEndPos);
}

void TransformedPattern::replaceLocal(int patternStartPos, int patternEndPos, int lineEndPos)
{
	text = text.substr(patternStartPos, 0) + argumentChar + text.substr(patternEndPos);
	// insert a new keyframe after the argument char and check for any keyframes which got redundant
	int shift = (patternEndPos - patternStartPos) + std::string_view("\a").length();
	if (shift)
	{
		bool added{};
		const KeyFrame &newKeyFrame{.patternPos = patternEndPos, .linePos = lineEndPos};
		for (auto it = keyframes.begin(); it != keyframes.end();)
		{
			KeyFrame &keyFrame = *it;
			if (keyFrame.patternPos > patternStartPos)
			{
				if (keyFrame.patternPos >= patternEndPos)
				{
					if (!added)
					{
						added = true;
						// insert the new keyframe behind this keyframe
						it = keyframes.insert(it, newKeyFrame);
						// go past the new keyframe so the other ++ will go past the old keyframe
						it++;
					}
					// shift all keyframes on the right
					keyFrame.patternPos -= shift;
				}
				else
				{
					// remove this keyframe, it got replaced
					it = keyframes.erase(it);
					continue;
				}
			}
			it++;
		}
		if (!added)
		{
			// at the back
			keyframes.push_back(newKeyFrame);
		}
	}
}
