// @ of a missing attribute reference is 0; a segment loaded again in the frame it was unloaded in
// is compiled anew (its functions were invalidated at the unload)
object presets;

int test_count_of_missing()
{
	aref types = presets.missing$aref;
	int n = 0;
	for (int i = 0; i < @types; i++)
		n++;
	return @types == 0 && n == 0;
}

int test_reload_in_one_frame()
{
	if (!LoadSegment("segments_and_counts_part.h")) return 0;
	int first = SegmentPartValue();
	UnloadSegment("segments_and_counts_part.h");
	if (!LoadSegment("segments_and_counts_part.h")) return 0;
	int second = SegmentPartValue();
	UnloadSegment("segments_and_counts_part.h");
	return first == 5 && second == 5;
}

extern int SegmentPartValue();
