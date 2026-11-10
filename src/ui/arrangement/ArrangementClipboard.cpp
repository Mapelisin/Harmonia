#include "ui/arrangement/ArrangementModel.h"
#include "audio/Consolidate.h"
#include "core/AutomationPieces.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/Tracks.h"

// ArrangementModel: copy, cut and paste of clips, and the commands on a selected piece of time.

namespace harmonia::ui::arrangement
{
    namespace clips = core::clips;

    //==============================================================================
    namespace
    {
        constexpr double smallestPieceBeats = 1.0 / 64.0;       // of a clip, after a cut

        /** What Ctrl+C keeps: copies of the clips' states, and where each was relative to the
            earliest clip and the topmost track. Of a piece of time, a clip may be only a part:
            `shown` is the part (song beats, where the clip was). */
        struct CopiedClip
        {
            te::ClipCopy copy;
            int trackOffset = 0;
            double beatOffset = 0.0;
            double wasAtBeat = 0.0;
            std::optional<juce::Range<double>> shown;
        };

        /** What the automation of a row does over a piece of time (the row counted from the
            piece's first). */
        struct CopiedLines
        {
            int trackOffset = 0;
            core::automation_pieces::Piece lines;
        };

        /** The clips copied; for a piece of time, how long it was, how many rows, and what the
            rows' automation did over it. */
        struct Clipboard
        {
            std::vector<CopiedClip> clips;
            double pieceBeats = 0.0;
            int pieceRows = 0;
            std::vector<CopiedLines> automation;

            bool isPiece() const noexcept       { return pieceRows > 0; }
        };

        Clipboard& getClipboard()
        {
            static Clipboard clipboard;
            return clipboard;
        }

        /** What a track has between two beats, as copies (the track isn't touched). */
        void copyRange (te::AudioTrack& track, double from, double to, int trackOffset, std::vector<CopiedClip>& into)
        {
            for (auto* clip : clips::getClips (track))
            {
                const auto start = clips::getStartBeat (*clip), end = clips::getEndBeat (*clip);
                const auto shownFrom = std::max (start, from), shownTo = std::min (end, to);

                if (shownTo - shownFrom < smallestPieceBeats)
                    continue;

                CopiedClip copied { te::ClipCopy::fromClip (*clip), trackOffset, shownFrom - from, start, std::nullopt };

                if (shownFrom > start + 1.0e-6 || shownTo < end - 1.0e-6)
                    copied.shown = juce::Range<double> (shownFrom, shownTo);

                into.push_back (std::move (copied));
            }
        }

        /** What a track has between two beats goes: clips are cut at the two beats, and what is
            between is removed. Into the current undo step. Returns how many clips it touched. */
        int deleteRange (te::AudioTrack& track, double from, double to)
        {
            const auto gone = clips::isolate (track, from, to, smallestPieceBeats);
            clips::remove (gone);
            return gone.size();
        }

        /** A copy on `track` starting at `startBeat`: the whole clip, or the part of it copied. */
        te::Clip* insertCopied (te::AudioTrack& track, const CopiedClip& copied, double startBeat)
        {
            if (! copied.shown.has_value())
                return clips::insertCopy (track, copied.copy, startBeat);

            // Where the clip was, cut down to the part; then to its place.
            auto* clip = clips::insertCopy (track, copied.copy, copied.wasAtBeat);

            if (clip == nullptr)
                return nullptr;

            clips::setStart (*clip, copied.shown->getStart(), smallestPieceBeats);
            clips::setEnd (*clip, copied.shown->getEnd(), smallestPieceBeats);
            clips::move (*clip, startBeat, track);
            return clip;
        }
    }

    int ArrangementModel::splitAt (double beat)
    {
        const auto covers = [beat] (te::Clip* clip)
        {
            return clips::getStartBeat (*clip) < beat && beat < clips::getEndBeat (*clip);
        };

        auto targets = getSelectedClips();
        targets.removeIf ([&covers] (te::Clip* clip) { return ! covers (clip); });

        if (targets.isEmpty())
            if (auto* track = getCurrentTrack())
                for (auto* clip : clips::getClips (*track))
                    if (covers (clip))
                        targets.add (clip);

        if (targets.isEmpty())
            return 0;

        project.beginGesture (targets.size() == 1 ? "Split clip" : "Split clips");
        juce::Array<te::Clip*> rightHalves;

        for (auto* clip : targets)
            if (auto* right = clips::split (*clip, beat))
                rightHalves.add (right);

        setSelection (rightHalves);
        clipsDirty = true;
        changed();
        return rightHalves.size();
    }

    int ArrangementModel::copySelected()
    {
        const auto selected = getSelectedClips();

        if (selected.isEmpty())
            return 0;

        auto firstBeat = std::numeric_limits<double>::max();
        auto topTrack = std::numeric_limits<int>::max();

        for (auto* clip : selected)
        {
            firstBeat = std::min (firstBeat, clips::getStartBeat (*clip));
            topTrack = std::min (topTrack, indexOf (clip->getTrack()));
        }

        auto& clipboard = getClipboard();
        clipboard = {};

        for (auto* clip : selected)
            clipboard.clips.push_back ({ te::ClipCopy::fromClip (*clip),
                                         indexOf (clip->getTrack()) - topTrack,
                                         clips::getStartBeat (*clip) - firstBeat,
                                         clips::getStartBeat (*clip), std::nullopt });

        return (int) clipboard.clips.size();
    }

    int ArrangementModel::cutSelected()
    {
        const auto count = copySelected();

        if (count > 0)
        {
            project.beginGesture (count == 1 ? "Cut clip" : "Cut clips");
            removeClips (getSelectedClips());
        }

        return count;
    }

    int ArrangementModel::paste (double beat, int row)
    {
        const auto& clipboard = getClipboard();

        if (clipboard.clips.empty())
            return 0;

        const auto tracks = getTracks();
        const auto baseTrack = row >= 0 ? row : std::max (0, indexOf (getCurrentTrack()));
        beat = std::max (0.0, beat);
        juce::Array<te::Clip*> pasted;

        if (clipboard.isPiece())
        {
            // A piece of time: row for row, in place of what is there.
            project.beginGesture ("Paste time");

            for (int i = 0; i < clipboard.pieceRows; ++i)
            {
                auto* track = getTrack (baseTrack + i);

                if (track == nullptr || isLockedRow (track))
                    continue;

                // A row that can't take what the piece has for it (notes onto a row of sound) is
                // left as it is; a row the piece has nothing for is emptied there (time is pasted,
                // silence too).
                auto fits = true;

                for (const auto& copied : clipboard.clips)
                    fits = fits && (copied.trackOffset != i || clips::canTake (*track, copied.copy));

                if (! fits)
                    continue;

                deleteRange (*track, beat, beat + clipboard.pieceBeats);

                for (const auto& copied : clipboard.clips)
                    if (copied.trackOffset == i)
                        if (auto* clip = insertCopied (*track, copied, beat + copied.beatOffset))
                            pasted.add (clip);

                // Its automation too, on the track it was copied from.
                for (const auto& copied : clipboard.automation)
                    if (copied.trackOffset == i)
                        core::automation_pieces::paste (*track, copied.lines, beat);
            }

            clipsDirty = true;
            validateCurrent();
            setTimeSelection (beat, beat + clipboard.pieceBeats, baseTrack, baseTrack + clipboard.pieceRows - 1);
            changed();
            return pasted.size();
        }

        project.beginGesture ("Paste clips");

        for (const auto& copied : clipboard.clips)
        {
            for (int i = baseTrack + copied.trackOffset; i < tracks.size(); ++i)
            {
                if (clips::canTake (*tracks[i], copied.copy))
                {
                    if (auto* clip = clips::insertCopy (*tracks[i], copied.copy, beat + copied.beatOffset))
                        pasted.add (clip);

                    break;
                }
            }
        }

        setSelection (pasted);
        clipsDirty = true;
        changed();
        return pasted.size();
    }

    bool ArrangementModel::canPaste()
    {
        return ! getClipboard().clips.empty();
    }

    bool ArrangementModel::hasCopiedTime()
    {
        return getClipboard().isPiece();
    }

    int ArrangementModel::getNumCopiedClips()
    {
        return (int) getClipboard().clips.size();
    }

    //==============================================================================
    int ArrangementModel::deleteTime()
    {
        const auto piece = getTimeSelection();

        if (! piece.has_value())
            return 0;

        // Is there anything in it at all? (Nothing: no undo step.)
        std::vector<CopiedClip> inside;

        for (int row = piece->firstRow; row <= piece->lastRow; ++row)
            if (auto* track = getTrack (row); track != nullptr && ! isLockedRow (track))
                copyRange (*track, piece->startBeat, piece->endBeat, 0, inside);

        if (inside.empty())
            return 0;

        project.beginGesture ("Delete time");
        int touched = 0;

        for (int row = piece->firstRow; row <= piece->lastRow; ++row)
            if (auto* track = getTrack (row); track != nullptr && ! isLockedRow (track))
                touched += deleteRange (*track, piece->startBeat, piece->endBeat);

        clipsDirty = true;
        validateCurrent();
        changed();
        return touched;
    }

    int ArrangementModel::copyTime()
    {
        const auto piece = getTimeSelection();

        if (! piece.has_value())
            return 0;

        Clipboard copied;
        copied.pieceBeats = piece->getLength();
        copied.pieceRows = piece->getNumRows();

        for (int row = piece->firstRow; row <= piece->lastRow; ++row)
        {
            if (auto* track = getTrack (row); track != nullptr && ! isLockedRow (track))
            {
                copyRange (*track, piece->startBeat, piece->endBeat, row - piece->firstRow, copied.clips);
                copied.automation.push_back ({ row - piece->firstRow, core::automation_pieces::copy (*track, piece->startBeat, piece->endBeat) });
            }
        }

        if (copied.clips.empty())
            return 0;

        const auto count = (int) copied.clips.size();
        getClipboard() = std::move (copied);
        return count;
    }

    int ArrangementModel::cutTime()
    {
        const auto count = copyTime();

        if (count > 0)
        {
            const auto piece = getTimeSelection();
            deleteTime();

            // What is cut is taken away, to be put elsewhere: its automation with it. (What is
            // deleted leaves its automation: a line isn't a clip.)
            if (piece.has_value())
                for (int row = piece->firstRow; row <= piece->lastRow; ++row)
                    if (auto* track = getTrack (row); track != nullptr && ! isLockedRow (track))
                        core::automation_pieces::clear (*track, piece->startBeat, piece->endBeat);

            project.getEdit().getUndoManager().setCurrentTransactionName ("Cut time");
        }

        return count;
    }

    int ArrangementModel::duplicateTime()
    {
        const auto piece = getTimeSelection();

        if (! piece.has_value())
            return 0;

        std::vector<CopiedClip> copies;

        for (int row = piece->firstRow; row <= piece->lastRow; ++row)
            if (auto* track = getTrack (row); track != nullptr && ! isLockedRow (track))
                copyRange (*track, piece->startBeat, piece->endBeat, row - piece->firstRow, copies);

        if (copies.empty())
            return 0;

        project.beginGesture ("Duplicate time");
        const auto to = piece->endBeat;
        juce::Array<te::Clip*> made;

        // The rows' automation is repeated with the clips.
        for (int row = piece->firstRow; row <= piece->lastRow; ++row)
            if (auto* track = getTrack (row); track != nullptr && ! isLockedRow (track))
                core::automation_pieces::paste (*track, core::automation_pieces::copy (*track, piece->startBeat, piece->endBeat), to);

        for (int row = piece->firstRow; row <= piece->lastRow; ++row)
            if (auto* track = getTrack (row); track != nullptr && ! isLockedRow (track))
                deleteRange (*track, to, to + piece->getLength());

        for (const auto& copied : copies)
            if (auto* track = getTrack (piece->firstRow + copied.trackOffset))
                if (auto* clip = insertCopied (*track, copied, to + copied.beatOffset))
                    made.add (clip);

        clipsDirty = true;
        validateCurrent();
        setTimeSelection (to, to + piece->getLength(), piece->firstRow, piece->lastRow);
        changed();
        return made.size();
    }

    int ArrangementModel::splitTime()
    {
        const auto piece = getTimeSelection();

        if (! piece.has_value())
            return 0;

        // The clips an edge of the piece goes through.
        std::vector<std::pair<te::Clip*, double>> cuts;

        for (int row = piece->firstRow; row <= piece->lastRow; ++row)
        {
            auto* track = getTrack (row);

            if (track == nullptr || isLockedRow (track))
                continue;

            for (auto* clip : clips::getClips (*track))
                for (const auto beat : { piece->endBeat, piece->startBeat })       // the later cut first: the clip keeps its first part
                    if (clips::getStartBeat (*clip) < beat - smallestPieceBeats && beat < clips::getEndBeat (*clip) - smallestPieceBeats)
                        cuts.push_back ({ clip, beat });
        }

        if (cuts.empty())
            return 0;

        project.beginGesture (cuts.size() == 1 ? "Split clip" : "Split clips");
        int made = 0;

        for (const auto& [clip, beat] : cuts)
            if (clips::split (*clip, beat) != nullptr)
                ++made;

        clipsDirty = true;
        changed();
        return made;
    }

    juce::String ArrangementModel::moveTime (double deltaBeats, int deltaRows, bool copy)
    {
        const auto piece = getTimeSelection();

        if (! piece.has_value() || (std::abs (deltaBeats) < 1.0e-9 && deltaRows == 0))
            return {};

        if (piece->startBeat + deltaBeats < -1.0e-9)
            return "It can't go before the start of the song";

        if (piece->firstRow + deltaRows < 0 || piece->lastRow + deltaRows >= getNumTracks())
            return "There are no tracks there";

        std::vector<CopiedClip> copies;

        for (int row = piece->firstRow; row <= piece->lastRow; ++row)
            if (auto* track = getTrack (row); track != nullptr && ! isLockedRow (track))
                copyRange (*track, piece->startBeat, piece->endBeat, row - piece->firstRow, copies);

        // Every piece of clip must be able to go where it would land.
        for (const auto& copied : copies)
        {
            auto* target = getTrack (piece->firstRow + deltaRows + copied.trackOffset);

            if (target == nullptr || isLockedRow (target))
                return "It can't go onto a frozen track or a folded folder";

            if (! clips::canTake (*target, copied.copy))
                return "Notes go on instrument tracks, sound on audio tracks";
        }

        const auto to = std::max (0.0, piece->startBeat + deltaBeats);
        project.beginGesture (copy ? "Copy time" : "Move time");

        // The rows' automation goes along while the piece stays on its rows (another row has
        // other knobs).
        if (deltaRows == 0)
        {
            for (int row = piece->firstRow; row <= piece->lastRow; ++row)
            {
                auto* track = getTrack (row);

                if (track == nullptr || isLockedRow (track))
                    continue;

                const auto lines = core::automation_pieces::copy (*track, piece->startBeat, piece->endBeat);

                if (! copy)
                    core::automation_pieces::clear (*track, piece->startBeat, piece->endBeat);

                core::automation_pieces::paste (*track, lines, to);
            }
        }

        if (! copy)
            for (int row = piece->firstRow; row <= piece->lastRow; ++row)
                if (auto* track = getTrack (row); track != nullptr && ! isLockedRow (track))
                    deleteRange (*track, piece->startBeat, piece->endBeat);

        for (int row = piece->firstRow; row <= piece->lastRow; ++row)
            if (auto* track = getTrack (row + deltaRows); track != nullptr && ! isLockedRow (track))
                deleteRange (*track, to, to + piece->getLength());

        for (const auto& copied : copies)
            if (auto* track = getTrack (piece->firstRow + deltaRows + copied.trackOffset))
                insertCopied (*track, copied, to + copied.beatOffset);

        clipsDirty = true;
        validateCurrent();
        setTimeSelection (to, to + piece->getLength(), piece->firstRow + deltaRows, piece->lastRow + deltaRows);
        changed();
        return {};
    }

    bool ArrangementModel::loopTime()
    {
        const auto piece = getTimeSelection();

        if (! piece.has_value())
            return false;

        setLoopRegion ({ piece->startBeat, piece->endBeat });
        setPlayMode (PlayMode::song);
        return true;
    }

    ArrangementModel::Consolidated ArrangementModel::consolidate()
    {
        // The stretch of each row: the selected time, or from the first to the last of the
        // clips selected on a track.
        struct Stretch
        {
            te::AudioTrack* track;
            double from, to;
        };

        std::vector<Stretch> stretches;
        const auto time = getTimeSelection();

        if (time.has_value())
        {
            for (int row = time->firstRow; row <= time->lastRow; ++row)
                if (auto* track = getTrack (row); track != nullptr && ! isLockedRow (track))
                    stretches.push_back ({ track, time->startBeat, time->endBeat });
        }
        else
        {
            for (auto* clip : getSelectedClips())
            {
                auto* track = dynamic_cast<te::AudioTrack*> (clip->getTrack());

                if (track == nullptr || isLockedRow (track))
                    continue;

                const auto start = clips::getStartBeat (*clip), end = clips::getEndBeat (*clip);
                const auto known = std::find_if (stretches.begin(), stretches.end(), [track] (const Stretch& s) { return s.track == track; });

                if (known == stretches.end())
                    stretches.push_back ({ track, start, end });
                else
                    *known = { track, std::min (known->from, start), std::max (known->to, end) };
            }
        }

        // What can fail (rendering sound) first, the song untouched.
        Consolidated result;
        std::vector<audio::consolidate::Piece> pieces;

        for (const auto& stretch : stretches)
        {
            juce::String error;
            const auto count = audio::consolidate::countClips (*stretch.track, stretch.from, stretch.to);

            if (auto piece = audio::consolidate::prepare (*stretch.track, stretch.from, stretch.to, error))
            {
                pieces.push_back (*piece);
                result.clipsBefore += count;
            }
            else if (error.isNotEmpty())
            {
                // The files rendered so far are nobody's.
                for (const auto& made : pieces)
                    made.sound.deleteFile();

                result = {};
                result.problem = error;
                return result;
            }
        }

        if (pieces.empty())
            return result;

        project.beginGesture ("Consolidate");
        juce::Array<te::Clip*> joined;

        for (const auto& piece : pieces)
            if (auto* clip = audio::consolidate::apply (project, piece))
                joined.add (clip);

        result.rows = joined.size();
        clipsDirty = true;
        validateCurrent();

        if (! time.has_value())
            setSelection (joined);

        changed();
        return result;
    }
}
