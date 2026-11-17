#include "PluginEditor.h"
#include "Params.h"

namespace octaline
{

namespace
{
constexpr int kStripW = 128;
constexpr int kStripH = 676;
constexpr int kGap = 6;
constexpr int kMargin = 12;
constexpr int kHeaderH = 52;
constexpr int kGlobalW = 150;

// Band settings clipboard shared by all strips (routing is not copied).
std::map<juce::String, float>& bandClipboard()
{
    static std::map<juce::String, float> c;
    return c;
}
} // namespace

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption,
            const juce::String& tooltip)
    : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow),
      attachment (state, paramId, slider)
{
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 15);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (220);
    if (auto* p = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    slider.setTooltip (tooltip);
    addAndMakeVisible (slider);

    label.setText (caption, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (11.0f));
    label.setColour (juce::Label::textColourId, colours::textDim);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (13));
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, r.getWidth(), 15);
    slider.setBounds (r);
}

void Knob::setAccent (juce::Colour c)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
}

//==============================================================================
Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption,
                const juce::StringArray& items, const juce::String& tooltip)
{
    label.setText (caption, juce::dontSendNotification);
    label.setFont (juce::FontOptions (11.0f));
    label.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (label);

    combo.addItemList (items, 1);
    combo.setTooltip (tooltip);
    addAndMakeVisible (combo);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, combo);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromLeft (40));
    combo.setBounds (r.reduced (0, 1));
}

//==============================================================================
static juce::StringArray numberedItems (const juce::String& first, int from, int to)
{
    juce::StringArray s { first };
    for (int k = from; k <= to; ++k)
        s.add (juce::String (k));
    return s;
}

static juce::StringArray noteItems()
{
    juce::StringArray s;
    for (auto& n : params::noteValues())
        s.add (n.name);
    return s;
}

BandStrip::BandStrip (OctalineProcessor& p, int band1)
    : processor (p), state (p.getState()), band (band1), accent (colours::band (band1 - 1)),
      source (state, params::bandId (band1, params::kSource), "Input", numberedItems ("IN", 1, 7),
              "Where this band gets its signal: the plugin input (IN), or the output of a lower band for series chains."),
      group (state, params::bandId (band1, params::kGroup), "Group", numberedItems ("-", 2, 8),
             "Chain this band and the following ones into one long delay line, up to the band chosen here."),
      sync (state, params::bandId (band1, params::kSync), "Sync", numberedItems ("Off", 1, 8),
            "Lock this band's modulation rate to another band. Speed then sets the phase offset (5.0 = 180 degrees)."),
      wave (state, params::bandId (band1, params::kWave), "Wave", { "Sine", "Triangle", "Saw Up", "Saw Down" },
            "Modulation waveform."),
      note (state, params::bandId (band1, params::kNote), "Tempo", noteItems(),
            "Follow the host tempo with a note value, or Free to set the time in ms."),
      time (state, params::bandId (band1, params::kTime), "TIME", "Delay time. Grouped bands can go longer."),
      lowCut (state, params::bandId (band1, params::kLowCut), "LOW CUT", "Low cut inside the feedback loop. 10 = 1 kHz."),
      highCut (state, params::bandId (band1, params::kHighCut), "HIGH CUT", "High cut inside the feedback loop. 10 = 1 kHz."),
      feedback (state, params::bandId (band1, params::kFeedback), "FEEDBACK", "Number of repeats. 10 repeats forever."),
      tapKnob (state, params::bandId (band1, params::kTap), "TAP", "Where in the delay time the output is read, in percent."),
      speed (state, params::bandId (band1, params::kSpeed), "SPEED", "Modulation speed (phase offset when synced)."),
      depth (state, params::bandId (band1, params::kDepth), "DEPTH", "Modulation depth."),
      pan (state, params::bandId (band1, params::kPan), "PAN", "Position of this band's echoes."),
      level (state, params::bandId (band1, params::kLevel), "LEVEL", "Level of this band's echoes.")
{
    onButton.setClickingTogglesState (true);
    holdButton.setClickingTogglesState (true);
    phaseButton.setClickingTogglesState (true);
    onButton.setTooltip ("Switch this band on or off.");
    holdButton.setTooltip ("Stop new input entering this band while its echoes keep repeating.");
    tapButton.setTooltip ("Tap twice or more in tempo to set the delay time.");
    phaseButton.setTooltip ("Reverse the polarity of this band's echoes.");
    copyButton.setTooltip ("Copy this band's settings (not its routing).");
    pasteButton.setTooltip ("Paste copied band settings here.");

    onAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::bandId (band, params::kOn), onButton);
    holdAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::bandId (band, params::kHold), holdButton);
    phaseAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::bandId (band, params::kInvert), phaseButton);

    tapButton.onClick = [this] { tap(); };
    copyButton.onClick = [this] { copyBand(); };
    pasteButton.onClick = [this] { pasteBand(); };

    for (auto* c : std::initializer_list<juce::Component*> { &onButton, &holdButton, &tapButton, &phaseButton, &copyButton, &pasteButton,
                                                              &source, &group, &sync, &wave, &note,
                                                              &time, &lowCut, &highCut, &feedback, &tapKnob, &speed, &depth, &pan, &level })
        addAndMakeVisible (c);

    // Only lower bands can feed this one, and a group can only extend upwards.
    for (int k = 1; k <= 7; ++k)
        source.getCombo().setItemEnabled (k + 1, k < band);
    for (int k = 2; k <= 8; ++k)
        group.getCombo().setItemEnabled (k, k > band);
    sync.getCombo().setItemEnabled (band + 1, false);
}

void BandStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (accent.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (accent.withAlpha (0.85f));
    g.fillRoundedRectangle (r.removeFromTop (4.0f).reduced (6.0f, 0.0f), 2.0f);

    g.setColour (accent);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText (juce::String (band), 8, 6, 30, 28, juce::Justification::centredLeft);

    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (status, 6, 64, getWidth() - 12, 28, juce::Justification::centred, 2);
}

void BandStrip::resized()
{
    const int w = getWidth() - 12;
    onButton.setBounds (6 + w - 52, 8, 52, 24);

    auto row = juce::Rectangle<int> (6, 38, w, 22);
    holdButton.setBounds (row.removeFromLeft (w / 2 - 2));
    tapButton.setBounds (row.withTrimmedLeft (4));

    int y = 96;
    for (auto* c : std::initializer_list<juce::Component*> { &source, &group, &note, &sync, &wave })
    {
        c->setBounds (6, y, w, 24);
        y += 24;
    }
    phaseButton.setBounds (6, y + 2, w, 20);

    time.setBounds (6, 246, w, 94);

    y = 344;
    const int half = w / 2;
    for (auto pair : { std::pair<Knob*, Knob*> { &lowCut, &highCut }, { &feedback, &tapKnob }, { &speed, &depth }, { &pan, &level } })
    {
        pair.first->setBounds (6, y, half, 76);
        pair.second->setBounds (6 + half, y, w - half, 76);
        y += 76;
    }

    row = juce::Rectangle<int> (6, getHeight() - 26, w, 20);
    copyButton.setBounds (row.removeFromLeft (w / 2 - 2));
    pasteButton.setBounds (row.withTrimmedLeft (4));
}

void BandStrip::refresh (const std::array<BandRole, kNumBands>& roles)
{
    const auto& role = roles[(size_t) band - 1];
    const int head1 = role.head + 1;
    const bool member = ! role.isHead();
    const bool on = getParam (band, params::kOn) > 0.5f;
    const bool synced = getParam (head1, params::kNote) > 0.5f;
    const float ms = processor.getEffectiveTimeMs (band);

    juce::String s;
    if (member)
        s << "In group of band " << head1 << "\n" << params::formatMs (ms * getParam (band, params::kTap) / 100.0f) << " tap";
    else
    {
        const int src = (int) getParam (band, params::kSource);
        s << (role.span > 1 ? "Group " + juce::String (band) + "-" + juce::String (band + role.span - 1) : juce::String ("Single"))
          << (src > 0 && src < band ? "  from " + juce::String (src) : juce::String ("  from IN"))
          << "\n" << params::formatMs (ms) << (synced ? " (tempo)" : "");
    }
    if (! on)
        s = "Off";

    const auto newAccent = colours::band (role.head);
    if (s != status || newAccent != accent)
    {
        status = s;
        accent = newAccent;
        for (auto* k : { &time, &lowCut, &highCut, &feedback, &tapKnob, &speed, &depth, &pan, &level })
            k->setAccent (accent);
        onButton.setColour (juce::TextButton::buttonOnColourId, accent);
        repaint();
    }

    // Dim everything but the power button while the band is off (still editable).
    for (auto* c : getChildren())
        if (c != &onButton)
            c->setAlpha (on ? 1.0f : 0.45f);

    // Grouped members follow their head's time, filters and feedback.
    for (auto* c : std::initializer_list<juce::Component*> { &lowCut, &highCut, &feedback, &source, &group, &note })
        c->setEnabled (! member);
    time.setEnabled (! member && ! synced);
    holdButton.setEnabled (! member);
}

float BandStrip::getParam (int band1, const char* suffix) const
{
    if (auto* p = state.getParameter (params::bandId (band1, suffix)))
        return p->convertFrom0to1 (p->getValue());
    return 0.0f;
}

void BandStrip::setParam (int band1, const char* suffix, float realValue)
{
    if (auto* p = state.getParameter (params::bandId (band1, suffix)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        p->endChangeGesture();
    }
}

void BandStrip::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! tapTimes.empty() && now - tapTimes.back() > 6000.0)
        tapTimes.clear();
    tapTimes.push_back (now);
    if (tapTimes.size() > 4)
        tapTimes.erase (tapTimes.begin());
    if (tapTimes.size() < 2)
        return;

    const double interval = (tapTimes.back() - tapTimes.front()) / (double) (tapTimes.size() - 1);

    // In a group the time belongs to the head band.
    const auto roles = resolveRoles (processor.getEnginePreview());
    const int head1 = roles[(size_t) band - 1].head + 1;
    const float maxMs = maxTimeMsForSpan (roles[(size_t) head1 - 1].span);
    setParam (head1, params::kNote, 0.0f);
    setParam (head1, params::kTime, (float) juce::jlimit (0.02, (double) maxMs, interval));
}

void BandStrip::copyBand()
{
    auto& clip = bandClipboard();
    clip.clear();
    for (auto& suffix : params::bandSuffixes())
        if (suffix != params::kSource && suffix != params::kGroup)
            clip[suffix] = getParam (band, suffix.toRawUTF8());
}

void BandStrip::pasteBand()
{
    for (auto& [suffix, value] : bandClipboard())
        setParam (band, suffix.toRawUTF8(), value);
}

//==============================================================================
OctalineEditor::OctalineEditor (OctalineProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      effect (p.getState(), params::kEffect, "EFFECT", "Level of the signal sent into the bands."),
      direct (p.getState(), params::kDirect, "DIRECT", "Level of the dry signal."),
      directPan (p.getState(), params::kDirectPan, "DIRECT PAN", "Position of the dry signal."),
      output (p.getState(), params::kOutput, "OUTPUT", "Overall output level. Not changed by presets.")
{
    setLookAndFeel (&lnf);

    for (int b = 1; b <= kNumBands; ++b)
        addAndMakeVisible (strips.add (new BandStrip (p, b)));

    for (auto* b : { &prevButton, &nextButton, &presetButton, &saveButton, &saveAsButton, &deleteButton, &bypassButton })
        addAndMakeVisible (b);
    for (auto* k : { &effect, &direct, &directPan, &output })
        addAndMakeVisible (k);

    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    presetButton.setTooltip ("Choose a preset");
    saveButton.setTooltip ("Overwrite the current user preset");
    saveAsButton.setTooltip ("Save the current settings as a new preset");
    deleteButton.setTooltip ("Delete the current user preset");

    prevButton.onClick = [this] { processor.getPresets().loadPrevious(); timerCallback(); };
    nextButton.onClick = [this] { processor.getPresets().loadNext(); timerCallback(); };
    presetButton.onClick = [this] { showPresetMenu(); };
    saveButton.onClick = [this] { save(); };
    saveAsButton.onClick = [this] { saveAs(); };
    deleteButton.onClick = [this] { deletePreset(); };

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0675a));
    bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.getState(), params::kBypass, bypassButton);

    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setColour (juce::Label::textColourId, colours::textDim);
    bpmLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (bpmLabel);

    setSize (kMargin * 2 + kNumBands * kStripW + (kNumBands - 1) * kGap + kMargin + kGlobalW,
             kHeaderH + kStripH + kMargin);
    timerCallback();
    startTimerHz (15);
}

OctalineEditor::~OctalineEditor()
{
    stopTimer();
    dialog.reset();
    setLookAndFeel (nullptr);
}

void OctalineEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    g.setColour (colours::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("OCTALINE", kMargin, 0, 160, kHeaderH, juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("8-band delay", kMargin + 122, 3, 100, kHeaderH, juce::Justification::centredLeft);

    auto global = juce::Rectangle<int> (getWidth() - kMargin - kGlobalW, kHeaderH, kGlobalW, kStripH).toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (global, 6.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (global.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (colours::textDim);
    g.drawText ("MASTER", global.removeFromTop (30.0f), juce::Justification::centred);
}

void OctalineEditor::resized()
{
    auto header = juce::Rectangle<int> (kMargin, 0, getWidth() - kMargin * 2, kHeaderH).withSizeKeepingCentre (getWidth() - kMargin * 2, 28);
    header.removeFromLeft (240);
    deleteButton.setBounds (header.removeFromRight (70));
    header.removeFromRight (6);
    saveAsButton.setBounds (header.removeFromRight (80));
    header.removeFromRight (6);
    saveButton.setBounds (header.removeFromRight (60));
    header.removeFromRight (16);
    prevButton.setBounds (header.removeFromLeft (30));
    nextButton.setBounds (header.removeFromRight (30));
    presetButton.setBounds (header.reduced (4, 0));

    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (kMargin + i * (kStripW + kGap), kHeaderH, kStripW, kStripH);

    auto g = juce::Rectangle<int> (getWidth() - kMargin - kGlobalW, kHeaderH + 34, kGlobalW, kStripH - 34).reduced (18, 0);
    for (auto* k : { &effect, &direct, &directPan, &output })
    {
        k->setBounds (g.removeFromTop (110).withSizeKeepingCentre (g.getWidth(), 100));
        g.removeFromTop (12);
    }
    bypassButton.setBounds (g.removeFromTop (30));
    g.removeFromTop (10);
    bpmLabel.setBounds (g.removeFromTop (20));
}

void OctalineEditor::timerCallback()
{
    auto& presets = processor.getPresets();
    auto name = presets.getCurrentName();
    if (presets.isDirty())
        name << "  (edited)";
    if (presetButton.getButtonText() != name)
        presetButton.setButtonText (name);
    saveButton.setEnabled (presets.isCurrentUserPreset());
    deleteButton.setEnabled (presets.isCurrentUserPreset());

    bpmLabel.setText ("Host tempo " + juce::String (processor.getHostBpm(), 1) + " BPM", juce::dontSendNotification);

    const auto roles = resolveRoles (processor.getEnginePreview());
    for (auto* s : strips)
        s->refresh (roles);
}

void OctalineEditor::showPresetMenu()
{
    auto& presets = processor.getPresets();
    presets.refresh();
    const auto& entries = presets.getEntries();

    juce::PopupMenu menu, factory, user;
    std::map<juce::String, juce::PopupMenu> factoryGroups;
    juce::StringArray groupOrder;
    std::map<juce::String, juce::String> groupTitles;
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        const bool ticked = i == presets.getCurrentIndex();
        if (e.factory)
        {
            auto key = e.name.substring (0, 1) + "xx";
            if (! groupOrder.contains (key))
            {
                groupOrder.add (key);
                groupTitles[key] = key + "   " + e.name.fromFirstOccurrenceOf (" ", false, false) + " ...";
            }
            factoryGroups[key].addItem (i + 1, e.name, true, ticked);
        }
        else
            user.addItem (i + 1, e.name, true, ticked);
    }
    for (auto& key : groupOrder)
        factory.addSubMenu (groupTitles[key], factoryGroups[key]);

    constexpr int kInit = 10000, kOpenFolder = 10001;
    menu.addSectionHeader ("Factory");
    menu.addSubMenu ("Factory presets", factory);
    menu.addSectionHeader ("Your presets");
    if (user.getNumItems() == 0)
        menu.addItem (-1, "(none yet - use Save As)", false);
    else
        menu.addSubMenu ("User presets", user);
    menu.addSeparator();
    menu.addItem (kInit, "Init (clear all bands)");
    menu.addItem (kOpenFolder, "Show presets folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
                        [this, safe = juce::Component::SafePointer<OctalineEditor> (this)] (int result) {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& pm = processor.getPresets();
                            if (result == kInit)
                                pm.loadInit();
                            else if (result == kOpenFolder)
                            {
                                auto folder = PresetManager::userFolder();
                                folder.createDirectory();
                                folder.startAsProcess();
                            }
                            else
                                pm.load (result - 1);
                            timerCallback();
                        });
}

void OctalineEditor::saveAs()
{
    dialog = std::make_unique<juce::AlertWindow> ("Save preset", "Name for the new preset:", juce::MessageBoxIconType::NoIcon, this);
    auto suggested = processor.getPresets().getCurrentName();
    dialog->addTextEditor ("name", suggested);
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, safe = juce::Component::SafePointer<OctalineEditor> (this)] (int r) {
        if (safe == nullptr || dialog == nullptr)
            return;
        const auto name = dialog->getTextEditorContents ("name");
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->dialog.reset(); });
        if (r != 1)
            return;

        const auto file = PresetManager::userFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + PresetManager::kExtension);
        auto doSave = [this, safe, name] {
            if (safe == nullptr) return;
            const auto err = processor.getPresets().saveAs (name);
            if (err.isNotEmpty()) showError (err);
            timerCallback();
        };
        if (file.existsAsFile())
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                              .withTitle ("Replace preset?")
                                              .withMessage ("A preset called \"" + file.getFileNameWithoutExtension() + "\" already exists. Replace it?")
                                              .withButton ("Replace")
                                              .withButton ("Cancel")
                                              .withAssociatedComponent (this),
                                          [doSave] (int choice) { if (choice == 1) doSave(); });
        }
        else
            doSave();
    }), false);
}

void OctalineEditor::save()
{
    const auto err = processor.getPresets().saveCurrent();
    if (err.isNotEmpty())
        showError (err);
    timerCallback();
}

void OctalineEditor::deletePreset()
{
    auto& presets = processor.getPresets();
    if (! presets.isCurrentUserPreset())
        return;
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("Delete preset?")
                                      .withMessage ("Move \"" + presets.getCurrentName() + "\" to the trash?")
                                      .withButton ("Delete")
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [this, safe = juce::Component::SafePointer<OctalineEditor> (this)] (int choice) {
                                      if (safe == nullptr || choice != 1) return;
                                      const auto err = processor.getPresets().deleteCurrent();
                                      if (err.isNotEmpty()) showError (err);
                                      timerCallback();
                                  });
}

void OctalineEditor::showError (const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("Octaline")
                                      .withMessage (message)
                                      .withButton ("OK")
                                      .withAssociatedComponent (this),
                                  nullptr);
}

} // namespace octaline
