#include "range.h"
#include <vector>
#include "pattern/pattern_tree/patternElement.h"
#include <ranges>
#include <functional>
#include <list>

//the BEL charachter is supposed to ring a bell in terminals. since this is code, there's no reason for the user to need this charachter.
constexpr char argumentChar = '\a';
/**
 * when modifying a line, you'll be able to find where a part of the line was first.
 * for example, if the line was 'i like bread' first and it becomes 'i $ bread',
 * then a keyframe will be added at the space in the pattern behind the '$' to point to the space in the pattern behind the 'like'
 */
struct TransformedPattern
{
	struct KeyFrame
	{
		int patternPos;
		int linePos;
	};
	typedef std::function<int(const KeyFrame&)> keyFrameTransformer;
	std::string text;
	std::vector<PatternElement> elements;
	std::list<KeyFrame> keyframes;
	TransformedPattern(std::string pattern);

	inline int transformPosition(int position, keyFrameTransformer keyFrameToInpos, keyFrameTransformer keyFrameToOutPos);
	inline int getLinePos(int patternPos);
	inline int getPatternPos(int linePos);
	//replace this part of the pattern with a type element.
	void replace(int lineStartPos, int lineEndPos);
	void replaceLocal(int patternStartPos, int patternEndPos, int lineEndPos);
};