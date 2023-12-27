/*
 // Copyright (c) 2021-2022 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

struct TextObjectHelper {

    inline static int minWidth = 3;

    
    static Rectangle<int> recalculateTextObjectBounds(t_canvas* patch, t_gobj* obj, String const& currentText, int& numLines, bool applyOffset = false, int maxIolets = 0)
    {
        int const fontHeight = 15;
        
        int x, y, w, h;
        pd::Interface::getObjectBounds(patch, obj, &x, &y, &w, &h);

        auto fontWidth = glist_fontwidth(static_cast<t_glist*>(patch));
        int idealTextWidth = getIdealWidthForText(currentText);

        // For regular text object, we want to adjust the width so ideal text with aligns with fontWidth
        int offset = applyOffset ? idealTextWidth % fontWidth : 0;
        int charWidth = getWidthInChars(obj);

        if (currentText.isEmpty()) { // If text is empty, set to minimum width
            w = std::max(charWidth, minWidth) * fontWidth;
        } else if (charWidth == 0) { // If width is set to automatic, calculate based on text width
            w = std::clamp(idealTextWidth, minWidth * fontWidth, fontWidth * 60);
        } else { // If width was set manually, calculate what the width is
            w = std::max(charWidth, minWidth) * fontWidth + offset;
        }

        w = std::max(w, maxIolets * 18);

        numLines = getNumLines(currentText, w, fontHeight);

        // Calculate height so that height with 1 line is 21px, after that scale along with fontheight
        h = numLines * fontHeight + (21.f - fontHeight);

        return { x, y, w, h };
    }

    static int getWidthInChars(void* ptr)
    {
        return static_cast<t_text*>(ptr)->te_width;
    }

    static int setWidthInChars(void* ptr, int newWidth)
    {
        return static_cast<t_text*>(ptr)->te_width = newWidth;
    }

    static std::unique_ptr<ComponentBoundsConstrainer> createConstrainer(Object* object)
    {
        class TextObjectBoundsConstrainer : public ComponentBoundsConstrainer {
        public:
            Object* object;

            explicit TextObjectBoundsConstrainer(Object* parent)
                : object(parent)
            {
            }
            /*
             * Custom version of checkBounds that takes into consideration
             * the padding around plugdata node objects when resizing
             * to allow the aspect ratio to be interpreted correctly.
             * Otherwise resizing objects with an aspect ratio will
             * resize the object size **including** margins, and not follow the
             * actual size of the visible object
             */
            void checkBounds(Rectangle<int>& bounds,
                Rectangle<int> const& old,
                Rectangle<int> const& limits,
                bool isStretchingTop,
                bool isStretchingLeft,
                bool isStretchingBottom,
                bool isStretchingRight) override
            {
                auto* patch = object->cnv->patch.getPointer().get();
                if (!patch)
                    return;

                auto fontWidth = glist_fontwidth(patch);

                // Remove margin
                auto newBounds = bounds.reduced(Object::margin);
                auto oldBounds = old.reduced(Object::margin);

                auto maxIolets = std::max({ 1, object->numInputs, object->numOutputs });
                auto minimumWidth = std::max(TextObjectHelper::minWidth, (maxIolets * 18) / fontWidth);

                // Calculate the width in text characters for both
                auto oldCharWidth = oldBounds.getWidth() / fontWidth;
                auto newCharWidth = std::max(minimumWidth, newBounds.getWidth() / fontWidth);

                // If we're resizing the left edge, move the object left
                if (isStretchingLeft) {
                    auto widthDiff = (newCharWidth - oldCharWidth) * fontWidth;
                    auto x = oldBounds.getX() - widthDiff;
                    auto y = oldBounds.getY(); // don't allow y resize

                    pd::Interface::moveObject(static_cast<t_glist*>(patch), static_cast<t_gobj*>(object->getPointer()), x - object->cnv->canvasOrigin.x, y - object->cnv->canvasOrigin.y);
                }

                // Set new width
                TextObjectHelper::setWidthInChars(object->getPointer(), newCharWidth);
                
                bounds = object->gui->getPdBounds().expanded(Object::margin) + object->cnv->canvasOrigin;
            }
        };

        return std::make_unique<TextObjectBoundsConstrainer>(object);
    }

    static String fixNewlines(String text)
    {
        // Don't want \r
        text = text.replace("\r", "");

        // Temporarily use \r to represent a real newline in pd
        text = text.replace(";\n", "\r");

        // Remove \n
        text = text.replace("\n", " ");

        // Replace the real newlines with \n
        text = text.replace("\r", ";\n");

        // Remove whitespace from end
        text = text.trimEnd();

        return text;
    }

    static int getIdealWidthForText(String const& text)
    {
        
        auto lines = StringArray::fromLines(text);
        int w = minWidth;

        for (auto& line : lines) {
            w = std::max<int>(CachedStringWidth<15>::calculateStringWidth(line) + 14, w);
        }

        return w;
    }

    // Used by text objects for estimating best text height for a set width
    static int getNumLines(String const& text, int width, int fontSize)
    {
        int numLines = 1;

        Array<int> glyphs;
        Array<float> xOffsets;

        auto font = Font(fontSize);
        font.getGlyphPositions(text.trimCharactersAtEnd(";\n"), glyphs, xOffsets);

        wchar_t lastChar;
        for (int i = 0; i < xOffsets.size(); i++) {
            if ((xOffsets[i] + 12) >= static_cast<float>(width) || (text.getCharPointer()[i] == '\n' && lastChar == ';')) {
                for (int j = i + 1; j < xOffsets.size(); j++) {
                    xOffsets.getReference(j) -= xOffsets[i];
                }
                numLines++;
            }
            lastChar = text.getCharPointer()[i];
        }

        return numLines;
    }

    static TextEditor* createTextEditor(Object* object, int fontHeight)
    {
        auto* editor = new TextEditor;
        editor->applyFontToAllText(Font(fontHeight));

        object->copyAllExplicitColoursTo(*editor);
        editor->setColour(TextEditor::textColourId, object->findColour(PlugDataColour::canvasTextColourId));
        editor->setColour(TextEditor::backgroundColourId, Colours::transparentBlack);
        editor->setColour(TextEditor::focusedOutlineColourId, Colours::transparentBlack);

        editor->setAlwaysOnTop(true);
        editor->setMultiLine(true);
        editor->setReturnKeyStartsNewLine(false);
        editor->setScrollbarsShown(false);
        editor->setIndents(0, 0);
        editor->setScrollToShowCursor(false);
        editor->setJustification(Justification::centredLeft);

        return editor;
    }
};

// Text base class that text objects with special implementation details can derive from
class TextBase : public ObjectBase
    , public TextEditor::Listener {

protected:
    std::unique_ptr<TextEditor> editor;
    BorderSize<int> border = BorderSize<int>(1, 7, 2, 2);
    
    TextLayout textLayout;
    hash32 layoutTextHash;
    
    Value sizeProperty = SynchronousValue();
    String objectText;
    bool isValid = true;
    bool isLocked;

public:
    TextBase(pd::WeakReference obj, Object* parent, bool valid = true)
        : ObjectBase(obj, parent)
        , isValid(valid)
    {
        objectText = getText();
        
        isLocked = getValue<bool>(cnv->locked);

        objectParameters.addParamInt("Width (chars)", cDimensions, &sizeProperty);
        updateTextLayout();
    }

    ~TextBase() override = default;

    void update() override
    {
        if (auto obj = ptr.get<t_text>()) {
            sizeProperty = TextObjectHelper::getWidthInChars(obj.get());
        }
    }

    void paint(Graphics& g) override
    {
        auto backgroundColour = object->findColour(PlugDataColour::textObjectBackgroundColourId);
        g.setColour(backgroundColour);
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), Corners::objectCornerRadius);

        auto ioletAreaColour = object->findColour(PlugDataColour::ioletAreaColourId);

        if (ioletAreaColour != backgroundColour) {
            g.setColour(ioletAreaColour);
            g.fillRect(getLocalBounds().toFloat().removeFromTop(3.5f));
            g.fillRect(getLocalBounds().toFloat().removeFromBottom(3.5f));
        }

        if (!editor) {
            auto textArea = border.subtractedFrom(getLocalBounds());
            textLayout.draw(g, textArea.toFloat());
        }
    }

    void paintOverChildren(Graphics& g) override
    {
        bool selected = object->isSelected() && !cnv->isGraph;

        auto outlineColour = object->findColour(selected ? PlugDataColour::objectSelectedOutlineColourId : objectOutlineColourId);

        if (!isValid) {
            outlineColour = selected ? Colours::red.brighter(1.5) : Colours::red;
        }

        g.setColour(outlineColour);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), Corners::objectCornerRadius, 1.0f);
    }

    // Override to cancel default behaviour
    void lock(bool locked) override
    {
        isLocked = locked;
    }

    void textEditorReturnKeyPressed(TextEditor& ed) override
    {
        if (editor != nullptr) {
            cnv->grabKeyboardFocus();
        }
    }

    void textEditorTextChanged(TextEditor& ed) override
    {
        object->updateBounds();
    }

    Rectangle<int> getPdBounds() override
    {
        updateTextLayout(); // make sure layout height is updated

        int x = 0, y = 0, w, h;
        if (auto obj = ptr.get<t_gobj>()) {
            auto* cnvPtr = cnv->patch.getPointer().get();
            if (!cnvPtr) return {x, y, getTextObjectWidth(), std::max<int>(textLayout.getHeight() + 7, 21)};
    
            pd::Interface::getObjectBounds(cnvPtr, obj.get(), &x, &y, &w, &h);
        }

        return {x, y, getTextObjectWidth(), std::max<int>(textLayout.getHeight() + 7, 21)};
    }
        
    int getTextObjectWidth()
    {
        auto objText = editor ? editor->getText() : objectText;
        if (editor && cnv->suggestor && cnv->suggestor->getText().isNotEmpty()) {
            objText = cnv->suggestor->getText();
        }
                
        int fontWidth = 7;
        int charWidth = 0;
        if (auto obj = ptr.get<void>()) {
            charWidth = TextObjectHelper::getWidthInChars(obj.get());
            fontWidth = glist_fontwidth(cnv->patch.getPointer().get());
        }
        
        // Calculating string width is expensive, so we cache all the strings that we already calculated the width for
        int idealWidth = CachedStringWidth<15>::calculateStringWidth(objText) + 12;
        
        // We want to adjust the width so ideal text with aligns with fontWidth
        int offset = idealWidth % fontWidth;
        
        int textWidth;
        if (objText.isEmpty()) { // If text is empty, set to minimum width
            textWidth = std::max(charWidth, TextObjectHelper::minWidth) * fontWidth;
        } else if (charWidth == 0) { // If width is set to automatic, calculate based on text width
            textWidth = std::clamp(idealWidth, TextObjectHelper::minWidth * fontWidth, fontWidth * 60);
        } else { // If width was set manually, calculate what the width is
            textWidth = std::max(charWidth, TextObjectHelper::minWidth) * fontWidth + offset;
        }
        
        return textWidth;
    }
        
    void updateTextLayout()
    {
        auto objText = editor ? editor->getText() : objectText;
        if (editor && cnv->suggestor && cnv->suggestor->getText().isNotEmpty()) {
            objText = cnv->suggestor->getText();
        }
        
        int textWidth = getTextObjectWidth() - 10; // Reserve a bit of extra space for the text margin
        auto currentLayoutHash = hash(objText) ^ textWidth;
        if(layoutTextHash != currentLayoutHash)
        {
            auto attributedText = AttributedString(objectText);
            attributedText.setColour(object->findColour(PlugDataColour::canvasTextColourId));
            attributedText.setJustification(Justification::centredLeft);
            attributedText.setFont(Font(15));
            
            textLayout = TextLayout();
            textLayout.createLayout(attributedText, textWidth);
            layoutTextHash = currentLayoutHash;
        }
    }

    void setPdBounds(Rectangle<int> b) override
    {
        if (auto gobj = ptr.get<t_gobj>()) {
            auto* patch = cnv->patch.getPointer().get();
            if (!patch)
                return;

            pd::Interface::moveObject(patch, gobj.get(), b.getX(), b.getY());

            if (TextObjectHelper::getWidthInChars(gobj.get())) {
                TextObjectHelper::setWidthInChars(gobj.get(), b.getWidth() / glist_fontwidth(patch));
            }

            auto type = hash(getText().upToFirstOccurrenceOf(" ", false, false));

            if (type == hash("inlet") || type == hash("inlet~")) {
                canvas_resortinlets(patch);
            } else if (type == hash("outlet") || type == hash("outlet~")) {
                canvas_resortoutlets(patch);
            }
        }
        
        updateTextLayout();
    }

    void mouseDown(MouseEvent const& e) override
    {
        if (!e.mods.isLeftButtonDown())
            return;

        if (isLocked) {
            click(e.getPosition(), e.mods.isShiftDown(), e.mods.isAltDown());
        }
    }

    bool showParametersWhenSelected() override
    {
        return false;
    }

    void hideEditor() override
    {
        if (editor != nullptr) {
            std::unique_ptr<TextEditor> outgoingEditor;
            std::swap(outgoingEditor, editor);

            cnv->hideSuggestions();

            auto newText = outgoingEditor->getText();

            outgoingEditor->removeListener(cnv->suggestor.get());

            newText = TextObjectHelper::fixNewlines(newText);

            bool changed;
            if (objectText != newText) {
                objectText = newText;
                updateTextLayout();
                repaint();
                changed = true;
            } else {
                changed = false;
            }

            outgoingEditor.reset();

            repaint();

            // update if the name has changed, or if pdobject is unassigned
            if (changed) {
                object->setType(newText);
            }
        }
    }

    bool isEditorShown() override
    {
        return editor != nullptr;
    }

    void showEditor() override
    {
        if (editor == nullptr) {
            editor.reset(TextObjectHelper::createTextEditor(object, 15));

            editor->setBorder(border);
            editor->setBounds(getLocalBounds());
            editor->setText(objectText, false);
            editor->addListener(this);
            editor->selectAll();

            addAndMakeVisible(editor.get());
            editor->grabKeyboardFocus();

            editor->onFocusLost = [this]() {
                if (reinterpret_cast<Component*>(cnv->suggestor.get())->hasKeyboardFocus(true) || Component::getCurrentlyFocusedComponent() == editor.get()) {
                    editor->grabKeyboardFocus();
                    return;
                }

                // TODO: this system is fragile
                // If anything grabs keyboard focus when clicking an object, this will close the editor!
                hideEditor();
            };

            cnv->showSuggestions(object, editor.get());

            resized();
            repaint();
        }
    }

    void updateSizeProperty() override
    {
        setPdBounds(object->getObjectBounds());

        if (auto text = ptr.get<t_text>()) {
            setParameterExcludingListener(sizeProperty, TextObjectHelper::getWidthInChars(text.get()));
        }
    }

    void valueChanged(Value& v) override
    {
        if (v.refersToSameSourceAs(sizeProperty)) {
            auto* constrainer = getConstrainer();
            auto width = std::max(getValue<int>(sizeProperty), constrainer->getMinimumWidth());

            setParameterExcludingListener(sizeProperty, width);

            if (auto text = ptr.get<t_text>()) {
                TextObjectHelper::setWidthInChars(text.get(), width);
            }

            object->updateBounds();
        }
    }

    void resized() override
    {
        if (editor) {
            editor->setBounds(getLocalBounds());
        }
        
        updateTextLayout();
    }

    /** Returns the currently-visible text editor, or nullptr if none is open. */
    TextEditor* getCurrentTextEditor() const
    {
        return editor.get();
    }

    bool hideInGraph() override
    {
        return true;
    }

    std::unique_ptr<ComponentBoundsConstrainer> createConstrainer() override
    {
        return TextObjectHelper::createConstrainer(object);
    }
};

// Actual text object, marked final for optimisation
class TextObject final : public TextBase {

public:
    TextObject(pd::WeakReference obj, Object* parent, bool isValid = true)
        : TextBase(obj, parent, isValid)
    {
    }
};
