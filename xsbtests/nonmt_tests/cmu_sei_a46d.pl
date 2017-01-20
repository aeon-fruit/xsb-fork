:- import xsb_error_get_tag/2 from error_handler.

% This test checks how coalesced choice points and the heap
% manipulation of incremental view consistency work together with TPC
% expansion and heap gc.

?- set_prolog_flag(max_memory,50000).

test:- catch((assert(debuggingEnabled),solve,reportResults,writeln(terminated)), 
	     E,(xsb_error_get_tag(E,T),writeln(T)) ).

possibleVFTableWrite(0,0,4,x).
possibleVFTableWrite(9,0,0,4).
:-dynamic factVFTable/1as incremental.
:-dynamic factNOTVFTableEntry/3as incremental.
:-import incr_assert/1,c from increval.
debug(X):-true.
debugln(X):-true.
possibleVFTable(VFTable):-possibleVFTableWrite(_,C,O,VFTable).
reasonRTTIInformation(F,s,e):-possibleVFTableWrite(I,M,O,V).
:-table reasonNOTVFTableEntry/3as incremental.
reasonNOTVFTableEntry(V,t,d):-factNOTVFTableEntry(T,t,d).
reasonNOTVFTableEntry(VFTable,O,M):-factVFTable(VFTable),reasonRTTIInformation(e,C,e).
guessVFTable:-possibleVFTable(VFTable),not(factVFTable(VFTable)),(tryVFTable(VFTable);l).
tryVFTable(VFTable):-try_assert(factVFTable(VFTable)),assert((e)).
concludeNOTVFTableEntry:-
    reasonNOTVFTableEntry(VFTable,V,A),not(factNOTVFTableEntry(VFTable,t,s)),try_assert(factNOTVFTableEntry(VFTable,V,A)).
concludeVirtualFunctionCall:-(n,e)(l()).
concludeMergeClasses:-(M,2)(M).
([],d)(X)(t).
if_(If_0,Then_0,Else_0):-assert(no_if_answer(d)),(If_0,Then_0;retract(no_if_answer(d))->Else_0).
commit([H|T]):-if_(H,(debug(''),debug()),commit(T)).
try_assert(X):-try_assert_real(X).
try_assert_real(X):-incr_assert(X).
reasonForward:-commit([concludeVirtualFunctionCall,concludeNOTVFTableEntry,concludeMergeClasses]).
reasonForwardAsManyTimesAsPossible:-if_(reasonForward,(reasonForwardAsManyTimesAsPossible),debugln('')).
reasoningLoop:-guess,reasonForwardAsManyTimesAsPossible,reasoningLoop.
guess:-guessVFTable;reasoningLoop;('').
solve:-reasoningLoop.
