// bst_visualiser.cpp
// Compile (Linux example):
// g++ bst_visualiser.cpp -o bst_visualiser -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
//
// On Windows (MinGW) you may need additional flags/adjustments per your raylib install.
//
// This file implements an interactive Binary Search Tree visualiser with animations, UI,
// and step-by-step operations: insertion, search, deletion, and traversals (in-order, pre-order, post-order).
// Author: ChatGPT (refined prompt)
//
// NOTE: This is a single-file self-contained program. You can refactor to multiple files later.

#include "raylib.h"
#include <cmath>
#include <string>
#include <vector>
#include <queue>
#include <functional>
#include <memory>
#include <sstream>
#include <algorithm>
#include <cassert>

using std::string;
using std::vector;
using std::queue;
using std::function;
using std::shared_ptr;
using std::make_shared;

// --------------------------- Configuration ---------------------------
static const int SCREEN_WIDTH = 1200;
static const int SCREEN_HEIGHT = 760;
static const int UI_HEIGHT = 100;
static const float NODE_RADIUS = 22.0f;
static const float LEVEL_STEP_Y = 90.0f;
static const float H_SPACING = 40.0f;
static const Color BG = (Color){30, 30, 40, 255};
static const Color UI_BG = (Color){20, 20, 28, 220};
static const Color NODE_COLOR = (Color){70, 130, 180, 255};
static const Color NODE_BORDER = (Color){30, 60, 80, 255};
static const Color TEXT_COLOR = WHITE;
static const Color HIGHLIGHT_CUR = (Color){250, 180, 20, 255};
static const Color HIGHLIGHT_COMP = (Color){200, 70, 70, 255};
static const Color HIGHLIGHT_TARGET = (Color){90, 200, 120, 255};

// Animation speed factor (will be adjustable)
float globalSpeed = 1.0f; // 0.25 (slow) ... 2.0 (fast)

// --------------------------- Utility functions ---------------------------
static string ToStr(int v) {
    std::ostringstream ss; ss << v; return ss.str();
}

// linear interpolation helpers
static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static Vector2 Lerp(const Vector2 &a, const Vector2 &b, float t) {
    return { Lerp(a.x, b.x, t), Lerp(a.y, b.y, t) };
}

// --------------------------- Animation Step System ---------------------------
// Each step is a small discrete action with optional duration and a completion callback.
// We'll keep a queue of steps and process them sequentially.

struct Step {
    float duration; // seconds
    // progress function called every frame with t from 0..1
    function<void(float)> onUpdate;
    // called once when step finishes
    function<void()> onFinish;

    Step(float d, function<void(float)> u = nullptr, function<void()> f = nullptr)
        : duration(d), onUpdate(u), onFinish(f) {}
};

class Animator {
private:
    queue<Step> steps;
    float elapsed = 0.0f;
    bool active = false;
public:
    void Clear() { while (!steps.empty()) steps.pop(); elapsed = 0; active = false; }
    void Push(const Step &s) { steps.push(s); if (!active) { elapsed = 0; active = true; } }
    bool IsBusy() const { return active; }
    void Update(float dt) {
        if (!active) return;
        if (steps.empty()) { active = false; return; }
        Step &cur = steps.front();
        float dur = cur.duration / globalSpeed;
        elapsed += dt;
        float t = dur <= 0 ? 1.0f : std::min(1.0f, elapsed / dur);
        if (cur.onUpdate) cur.onUpdate(t);
        if (t >= 1.0f) {
            if (cur.onFinish) cur.onFinish();
            steps.pop();
            elapsed = 0.0f;
            if (steps.empty()) active = false;
        }
    }
} animator;

// --------------------------- BST data structures ---------------------------
struct TreeNode {
    int value;
    shared_ptr<TreeNode> left = nullptr, right = nullptr, parent = nullptr;
    // Layout / animation state
    Vector2 pos = {0,0};
    Vector2 targetPos = {0,0}; // where it should be placed by layout
    Vector2 animFrom = {0,0}; // for move animation
    float animT = 1.0f;
    bool visible = true;
    bool highlighted = false;
    Color highlightColor = NODE_COLOR;

    TreeNode(int v): value(v) {}
};

class BST {
public:
    shared_ptr<TreeNode> root = nullptr;

    BST() {}
    void Clear() { root = nullptr; }

    // Normal BST insert (returns new node pointer)
    shared_ptr<TreeNode> InsertRaw(int value) {
        if (!root) {
            root = make_shared<TreeNode>(value);
            return root;
        }
        auto cur = root;
        shared_ptr<TreeNode> parent = nullptr;
        while (cur) {
            parent = cur;
            if (value < cur->value) cur = cur->left;
            else cur = cur->right;
        }
        auto node = make_shared<TreeNode>(value);
        node->parent = parent;
        if (value < parent->value) parent->left = node;
        else parent->right = node;
        return node;
    }

    // Find by value
    shared_ptr<TreeNode> Find(int value) {
        auto cur = root;
        while (cur) {
            if (value == cur->value) return cur;
            if (value < cur->value) cur = cur->left;
            else cur = cur->right;
        }
        return nullptr;
    }

    // Replace subtree u with v (helper for deletion)
    void Transplant(shared_ptr<TreeNode> u, shared_ptr<TreeNode> v) {
        if (u->parent == nullptr) {
            root = v;
        } else if (u == u->parent->left) {
            u->parent->left = v;
        } else {
            u->parent->right = v;
        }
        if (v) v->parent = u->parent;
    }

    // Find minimum in subtree
    shared_ptr<TreeNode> Minimum(shared_ptr<TreeNode> node) {
        auto cur = node;
        while (cur && cur->left) cur = cur->left;
        return cur;
    }

    // Raw delete (no animations) - returns true if deleted
    bool DeleteRaw(int value) {
        auto z = Find(value);
        if (!z) return false;
        if (!z->left) {
            Transplant(z, z->right);
        } else if (!z->right) {
            Transplant(z, z->left);
        } else {
            auto y = Minimum(z->right);
            if (y->parent != z) {
                Transplant(y, y->right);
                y->right = z->right;
                if (y->right) y->right->parent = y;
            }
            Transplant(z, y);
            y->left = z->left;
            if (y->left) y->left->parent = y;
        }
        return true;
    }

    // Traversals (collect nodes)
    void InOrder(shared_ptr<TreeNode> node, vector<shared_ptr<TreeNode>>& out) {
        if (!node) return;
        InOrder(node->left, out);
        out.push_back(node);
        InOrder(node->right, out);
    }
    void PreOrder(shared_ptr<TreeNode> node, vector<shared_ptr<TreeNode>>& out) {
        if (!node) return;
        out.push_back(node);
        PreOrder(node->left, out);
        PreOrder(node->right, out);
    }
    void PostOrder(shared_ptr<TreeNode> node, vector<shared_ptr<TreeNode>>& out) {
        if (!node) return;
        PostOrder(node->left, out);
        PostOrder(node->right, out);
        out.push_back(node);
    }

    // Utility: compute tree size (nodes)
    int Count(shared_ptr<TreeNode> n) {
        if (!n) return 0;
        return 1 + Count(n->left) + Count(n->right);
    }

    // Compute layout positions based on inorder index & depth
    void ComputeLayout() {
        // assign x by inorder traversal index; y by depth * LEVEL_STEP_Y
        vector<shared_ptr<TreeNode>> nodes;
        InOrder(root, nodes);
        // map node -> index
        std::unordered_map<TreeNode*, int> idx;
        for (int i=0;i<(int)nodes.size();++i) idx[nodes[i].get()] = i;

        // Depth-first to compute depth
        std::function<int(shared_ptr<TreeNode>)> depthOf = [&](shared_ptr<TreeNode> n)->int {
            if (!n) return -1;
            int d = 0;
            auto p = n->parent;
            while (p) { d++; p = p->parent; }
            return d;
        };

        // Compute spacing: full width is SCREEN_WIDTH minus margins
        const float leftMargin = 80.0f;
        const float rightMargin = 80.0f;
        float usable = (float)SCREEN_WIDTH - leftMargin - rightMargin;
        float stepX = (nodes.empty()) ? 0 : std::max(40.0f, usable / (float)std::max(1, (int)nodes.size()));

        for (auto &n : nodes) {
            int i = idx[n.get()];
            float x = leftMargin + i * stepX + stepX * 0.5f;
            int d = depthOf(n);
            float y = UI_HEIGHT + 40.0f + (d+1) * LEVEL_STEP_Y;
            n->targetPos = { x, y };
        }

        // For nodes not in the inorder list (shouldn't happen) place them at root area
        if (!root) return;
    }
};

// Global tree instance
BST tree;

// Camera for auto-zoom
Camera2D camera = { 0 };
float targetZoom = 1.0f;
Vector2 targetOffset = { 0, 0 };
const float CAMERA_LERP_SPEED = 4.0f;
const float MIN_ZOOM = 0.3f;
const float MAX_ZOOM = 1.5f;
const float ZOOM_PADDING = 80.0f; // Extra space around the tree

// --------------------------- Camera helpers ---------------------------
struct TreeBounds {
    float minX, maxX, minY, maxY;
    bool empty;
};

TreeBounds GetTreeBounds(shared_ptr<TreeNode> n) {
    TreeBounds bounds = { 0, 0, 0, 0, true };
    if (!n) return bounds;
    
    std::function<void(shared_ptr<TreeNode>)> traverse = [&](shared_ptr<TreeNode> node) {
        if (!node) return;
        if (bounds.empty) {
            bounds.minX = bounds.maxX = node->pos.x;
            bounds.minY = bounds.maxY = node->pos.y;
            bounds.empty = false;
        } else {
            if (node->pos.x < bounds.minX) bounds.minX = node->pos.x;
            if (node->pos.x > bounds.maxX) bounds.maxX = node->pos.x;
            if (node->pos.y < bounds.minY) bounds.minY = node->pos.y;
            if (node->pos.y > bounds.maxY) bounds.maxY = node->pos.y;
        }
        traverse(node->left);
        traverse(node->right);
    };
    traverse(n);
    return bounds;
}

void UpdateCamera(float dt) {
    // Calculate target camera position and zoom to fit tree
    TreeBounds bounds = GetTreeBounds(tree.root);
    
    if (bounds.empty) {
        // No tree, center on screen with default zoom
        targetZoom = 1.0f;
        targetOffset = { SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
    } else {
        // Add padding to bounds
        float treeWidth = bounds.maxX - bounds.minX + ZOOM_PADDING * 2;
        float treeHeight = bounds.maxY - bounds.minY + ZOOM_PADDING * 2;
        
        // Calculate center of tree
        float treeCenterX = (bounds.minX + bounds.maxX) / 2.0f;
        float treeCenterY = (bounds.minY + bounds.maxY) / 2.0f;
        
        // Calculate zoom to fit tree in view (accounting for UI areas)
        float viewWidth = SCREEN_WIDTH;
        float viewHeight = SCREEN_HEIGHT - UI_HEIGHT - 32; // Subtract UI top and bottom panels
        
        float zoomX = viewWidth / treeWidth;
        float zoomY = viewHeight / treeHeight;
        targetZoom = std::min(zoomX, zoomY);
        targetZoom = std::clamp(targetZoom, MIN_ZOOM, MAX_ZOOM);
        
        // Target offset to center the tree
        targetOffset.x = SCREEN_WIDTH / 2.0f - treeCenterX * targetZoom;
        targetOffset.y = (UI_HEIGHT + (SCREEN_HEIGHT - 32 - UI_HEIGHT) / 2.0f) - treeCenterY * targetZoom;
    }
    
    // Smooth lerp camera to target
    float lerpFactor = 1.0f - powf(0.001f, dt * CAMERA_LERP_SPEED);
    camera.zoom = Lerp(camera.zoom, targetZoom, lerpFactor);
    camera.offset = Lerp(camera.offset, targetOffset, lerpFactor);
    camera.target = { 0, 0 };
}

// --------------------------- Visual / UI helpers ---------------------------
struct Button {
    Rectangle rect;
    string label;
    bool pressed = false;
    bool disabled = false;
    function<void()> onClick;
    Button() {}
    Button(float x, float y, float w, float h, const string &l, function<void()> cb)
        : rect{ x,y,w,h }, label(l), onClick(cb) {}
    void Draw() {
        Color bg;
        Color textColor;
        if (disabled) {
            bg = Fade((Color){60,60,70,255}, 0.5f);
            textColor = Fade(TEXT_COLOR, 0.3f);
        } else {
            bg = pressed ? Fade((Color){150,150,160,255}, 0.95f) : Fade((Color){100,100,120,255}, 0.9f);
            textColor = TEXT_COLOR;
        }
        DrawRectangleRec(rect, bg);
        DrawRectangleLinesEx(rect, 2, disabled ? Fade((Color){40,40,60,255}, 0.5f) : (Color){40,40,60,255});
        int fontSize = 18;
        Vector2 m = MeasureTextEx(GetFontDefault(), label.c_str(), fontSize, 1);
        DrawText(label.c_str(), (int)(rect.x + (rect.width-m.x)/2), (int)(rect.y + (rect.height-m.y)/2), fontSize, textColor);
    }
    bool CheckClick(Vector2 mousePoint) {
        if (disabled) return false;
        if (CheckCollisionPointRec(mousePoint, rect)) {
            if (onClick) onClick();
            return true;
        }
        return false;
    }
};

struct UI {
    vector<Button> buttons;
    Rectangle inputRect;
    string inputText;
    bool typing = false;
    Button speedBtn;
    Button resetBtn;
    UI() {}
    void Init() {
        float x = 12;
        float y = 10;
        float w = 110;
        float h = 36;
        float gap = 12;
        buttons.clear();
        buttons.emplace_back(x, y, w, h, "Insert", [&]() { OnInsert(); });
        buttons.emplace_back(x + (w+gap), y, w, h, "Delete", [&]() { OnDelete(); });
        buttons.emplace_back(x + 2*(w+gap), y, w, h, "Search", [&]() { OnSearch(); });
        buttons.emplace_back(x + 3*(w+gap), y, w, h, "In-order", [&]() { OnTraversal("in"); });
        buttons.emplace_back(x + 4*(w+gap), y, w, h, "Pre-order", [&]() { OnTraversal("pre"); });
        buttons.emplace_back(x + 5*(w+gap), y, w, h, "Post-order", [&]() { OnTraversal("post"); });

        inputRect = { x, y + h + 12, 220, 36 };
        inputText.clear();

        speedBtn = Button(x + 240, y + h + 12, 110, 36, "Speed: x1", [&]() { ToggleSpeed(); });
        resetBtn = Button(x + 240 + 120, y + h + 12, 110, 36, "Reset", [&]() { Reset(); });

    }

    void Draw() {
        // Update disabled state based on animator
        bool isBusy = animator.IsBusy();
        for (auto &b : buttons) b.disabled = isBusy;
        speedBtn.disabled = false; // Speed button always enabled
        // resetBtn.disabled = false; // Reset button always enabled (can be used to cancel)
        
        // UI background top panel
        DrawRectangle(0, 0, SCREEN_WIDTH, UI_HEIGHT, UI_BG);
        // draw buttons
        for (auto &b : buttons) b.Draw();
        
        // Draw input field with disabled state
        Color inputBg = isBusy ? Fade((Color){255,255,255,10}, 0.1f) : Fade((Color){255,255,255,30}, 0.1f);
        Color inputBorder = isBusy ? Fade((Color){40,40,60,255}, 0.5f) : (Color){40,40,60,255};
        Color inputTextColor = isBusy ? Fade(TEXT_COLOR, 0.3f) : TEXT_COLOR;
        DrawRectangleRec(inputRect, inputBg);
        DrawRectangleLinesEx(inputRect, 2, inputBorder);
        string label = "Value: " + inputText;
        DrawText(label.c_str(), (int)(inputRect.x + 8), (int)(inputRect.y + 8), 20, inputTextColor);
        
        speedBtn.Draw();
        resetBtn.Draw();

        // small hints with status
        string hint = isBusy ? "Animation in progress... (Reset to cancel)" : "Click nodes to highlight / select (visual only). Press Enter after typing number.";
        DrawText(hint.c_str(), 12, UI_HEIGHT - 18, 14, Fade(TEXT_COLOR, 0.6f));
    }

    void HandleInput() {
        Vector2 mousePoint = GetMousePosition();

        // Buttons
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            // check main buttons
            for (auto &b : buttons) {
                if (b.CheckClick(mousePoint)) { b.pressed = true; return; }
            }
            if (CheckCollisionPointRec(mousePoint, inputRect)) {
                typing = true;
                return;
            } else {
                typing = false;
            }
            if (speedBtn.CheckClick(mousePoint)) { speedBtn.pressed = true; return; }
            if (resetBtn.CheckClick(mousePoint)) { resetBtn.pressed = true; return; }
        }
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            for (auto &b : buttons) b.pressed = false;
            speedBtn.pressed = false;
            resetBtn.pressed = false;
        }

        // Typing (only if not busy)
        if (typing && !animator.IsBusy()) {
            // Handle backspace
            if (IsKeyPressed(KEY_BACKSPACE)) {
                if (!inputText.empty()) inputText.pop_back();
            }
            
            // Handle enter
            if (IsKeyPressed(KEY_ENTER)) {
                typing = false;
                OnEnterPressed();
            }
            
            // Handle character input
            int key = GetCharPressed();
            while (key > 0) {
                // Allow digits (0-9) and minus sign for negative numbers
                if ((key >= 48 && key <= 57) || key == 45) {
                    inputText.push_back((char)key);
                }
                key = GetCharPressed();
            }
        } else {
            // global keyboard shortcuts
            if (IsKeyPressed(KEY_ENTER)) OnEnterPressed();
        }
    }

    int GetInputValue(bool &ok) {
        ok = false;
        if (inputText.empty()) return 0;
        try {
            int v = std::stoi(inputText);
            ok = true;
            return v;
        } catch (...) { ok = false; return 0; }
    }

    // Button callbacks (they call into higher-level functions)
    void OnInsert();
    void OnDelete();
    void OnSearch();
    void OnTraversal(const string &which);
    void ToggleSpeed();
    void Reset();
    void OnEnterPressed(); // called when user hits enter key after typing
} ui;

// Forward declarations for UI actions to interact with animator & tree
void AnimateInsert(int value);
void AnimateSearch(int value);
void AnimateDelete(int value);
void AnimateTraversal(const string &which);

// UI callbacks implement
void UI::OnInsert() {
    if (animator.IsBusy()) return;
    bool ok; int v = GetInputValue(ok);
    if (!ok) return;
    AnimateInsert(v);
}
void UI::OnDelete() {
    if (animator.IsBusy()) return;
    bool ok; int v = GetInputValue(ok);
    if (!ok) return;
    AnimateDelete(v);
}
void UI::OnSearch() {
    if (animator.IsBusy()) return;
    bool ok; int v = GetInputValue(ok);
    if (!ok) return;
    AnimateSearch(v);
}
void UI::OnTraversal(const string &which) {
    if (animator.IsBusy()) return;
    AnimateTraversal(which);
}
void UI::ToggleSpeed() {
    if (globalSpeed < 0.6f) globalSpeed = 1.0f;
    else if (globalSpeed < 1.6f) globalSpeed = 2.0f;
    else globalSpeed = 0.5f;
    string label = "Speed: x" + ToStr((int)round(globalSpeed));
    ui.speedBtn.label = label;
}
void UI::Reset() {
    animator.Clear();
    tree.Clear();
    ui.inputText.clear();
}
void UI::OnEnterPressed() {
    // default: Insert with Enter
    if (animator.IsBusy()) return;
    bool ok; int v = GetInputValue(ok);
    if (!ok) return;
    AnimateInsert(v);
}

// --------------------------- Drawing functions ---------------------------
void DrawEdge(const Vector2 &a, const Vector2 &b) {
    // draw a line from a to b offset by node radius
    Vector2 dir = { b.x - a.x, b.y - a.y };
    float len = sqrt(dir.x*dir.x + dir.y*dir.y);
    if (len <= 0.001f) return;
    Vector2 unit = { dir.x/len, dir.y/len };
    Vector2 start = { a.x + unit.x * NODE_RADIUS, a.y + unit.y * NODE_RADIUS };
    Vector2 end   = { b.x - unit.x * NODE_RADIUS, b.y - unit.y * NODE_RADIUS };
    DrawLineEx(start, end, 3.0f, (Color){200,200,210,220});
}

void DrawNode(shared_ptr<TreeNode> n) {
    if (!n) return;
    // border
    DrawCircleV(n->pos, NODE_RADIUS+2, NODE_BORDER);
    // fill
    Color col = n->highlighted ? n->highlightColor : NODE_COLOR;
    DrawCircleV(n->pos, NODE_RADIUS, col);
    // value
    string s = ToStr(n->value);
    int fs = 18;
    Vector2 m = MeasureTextEx(GetFontDefault(), s.c_str(), fs, 1);
    DrawText(s.c_str(), (int)(n->pos.x - m.x/2), (int)(n->pos.y - m.y/2), fs, TEXT_COLOR);
    // small parent pointer marker
    if (n->parent) {
        // nothing, edges drawn elsewhere
    }
}

// Recursively draw edges and nodes
void DrawTreeNodes(shared_ptr<TreeNode> n) {
    if (!n) return;
    if (n->left) {
        DrawEdge(n->pos, n->left->pos);
        DrawTreeNodes(n->left);
    }
    if (n->right) {
        DrawEdge(n->pos, n->right->pos);
        DrawTreeNodes(n->right);
    }
    DrawNode(n);
}

// --------------------------- Animation primitives for nodes ---------------------------
// We'll animate node moves by interpolating pos between animFrom and targetPos based on animT.

void UpdateNodeAnimations(shared_ptr<TreeNode> n, float dt) {
    if (!n) return;
    // simple linear progress already controlled by animator steps using onUpdate setting animT
    // We just ensure n->pos is up-to-date from animFrom/targetPos
    n->pos = Lerp(n->animFrom, n->targetPos, n->animT);
    if (n->left) UpdateNodeAnimations(n->left, dt);
    if (n->right) UpdateNodeAnimations(n->right, dt);
}

// --------------------------- High-level animation sequences ---------------------------

// helper to snapshot positions (set animFrom to current pos)
void SnapshotAllPositions(shared_ptr<TreeNode> n) {
    if (!n) return;
    n->animFrom = n->pos;
    n->animT = 0.0f;
    if (n->left) SnapshotAllPositions(n->left);
    if (n->right) SnapshotAllPositions(n->right);
}

// helper to set all target positions computed by layout and start move animation steps
void AnimateReflow(float duration = 0.6f) {
    tree.ComputeLayout();
    SnapshotAllPositions(tree.root);
    // push a step that interpolates animT from 0 to 1 across nodes
    animator.Push(Step(duration, [&](float t){
        // update every node animT
        std::function<void(shared_ptr<TreeNode>)> apply = [&](shared_ptr<TreeNode> n){
            if (!n) return;
            n->animT = t;
            n->pos = Lerp(n->animFrom, n->targetPos, n->animT);
            apply(n->left);
            apply(n->right);
        };
        apply(tree.root);
    }, nullptr));
}

// Insert animation: compare nodes step-by-step, highlight comparisons, then add node and reflow
void AnimateInsert(int value) {
    if (animator.IsBusy()) return; // don't start while busy
    // Step: traversing with comparisons; we will simulate path
    vector<shared_ptr<TreeNode>> path;
    auto cur = tree.root;
    if (!cur) {
        // tree empty: just create root with appear animation
        animator.Push(Step(0.01f, nullptr, [=](){
            auto n = tree.InsertRaw(value);
            n->pos = { SCREEN_WIDTH/2.0f, UI_HEIGHT + 40.0f };
            n->targetPos = n->pos;
            n->animFrom = n->pos;
            n->animT = 1.0f;
        }));
        // then reflow
        AnimateReflow(0.6f);
        return;
    }
    // Build path for visual comparisons
    while (cur) {
        path.push_back(cur);
        if (value < cur->value) cur = cur->left;
        else cur = cur->right;
    }
    // For each node in path, push step to highlight it (comparison)
    for (size_t i=0;i<path.size();++i) {
        auto node = path[i];
        animator.Push(Step(0.45f, [node](float t){
            node->highlighted = true;
            node->highlightColor = HIGHLIGHT_COMP;
            node->animT = Lerp(node->animT, 1.0f, t); // no-op but shows progression
        }, [node](){
            node->highlighted = false;
        }));
    }
    // After comparisons, create the new node visually near last comparison node and animate insertion
    // Use a shared pointer to track the specific node we insert
    shared_ptr<shared_ptr<TreeNode>> insertedNodeRef = make_shared<shared_ptr<TreeNode>>(nullptr);
    animator.Push(Step(0.02f, nullptr, [=](){
        // insert raw into data structure
        auto newNode = tree.InsertRaw(value);
        *insertedNodeRef = newNode; // Store reference to the actual inserted node
        // position it initially at same place as parent (or center)
        if (!newNode->parent) newNode->pos = { SCREEN_WIDTH/2.0f, UI_HEIGHT + 20.0f };
        else newNode->pos = newNode->parent->pos;
        newNode->animFrom = newNode->pos;
        newNode->targetPos = newNode->pos; // will be updated by reflow
        newNode->animT = 1.0f;
        newNode->visible = true;
        newNode->highlighted = true;
        newNode->highlightColor = HIGHLIGHT_TARGET;
    }));
    // tiny highlight of inserted node
    animator.Push(Step(0.4f, [=](float t){
        // pulse highlight
    }, [=](){
        // clear highlight on the specific node we inserted
        if (*insertedNodeRef) (*insertedNodeRef)->highlighted = false;
    }));
    // reflow tree positions with animation - do this in a step's onFinish so it happens after insertion
    animator.Push(Step(0.01f, nullptr, [=](){
        AnimateReflow(0.6f);
    }));
}

// Search animation: step-by-step highlight nodes, stop if found
void AnimateSearch(int value) {
    if (animator.IsBusy()) return;
    auto cur = tree.root;
    if (!cur) {
        // nothing
        return;
    }
    while (cur) {
        auto node = cur;
        animator.Push(Step(0.45f, [node](float t){
            node->highlighted = true;
            node->highlightColor = HIGHLIGHT_COMP;
        }, [node](){
            node->highlighted = false;
        }));
        if (value == cur->value) {
            // found: highlight target and stop with final highlight
            animator.Push(Step(0.6f, [node](float t){
                node->highlighted = true;
                node->highlightColor = HIGHLIGHT_CUR;
            }, [node](){
                node->highlighted = false;
            }));
            return;
        } else if (value < cur->value) cur = cur->left;
        else cur = cur->right;
    }
    // not found -> small feedback beep like highlight top area (we'll just a longer pause)
    animator.Push(Step(0.4f, nullptr, nullptr));
}

// Deletion animation: we will show comparisons to locate node, highlight it, then animate structural changes.
void AnimateDelete(int value) {
    if (animator.IsBusy()) return;
    auto z = tree.Find(value);
    // First traverse and highlight path
    auto cur = tree.root;
    if (!cur) return;
    vector<shared_ptr<TreeNode>> path;
    while (cur) {
        path.push_back(cur);
        if (value == cur->value) break;
        if (value < cur->value) cur = cur->left;
        else cur = cur->right;
    }
    for (auto &node : path) {
        animator.Push(Step(0.4f, [node](float t){
            node->highlighted = true;
            node->highlightColor = HIGHLIGHT_COMP;
        }, [node](){
            node->highlighted = false;
        }));
    }
    if (!z) {
        // value not found: brief pause
        animator.Push(Step(0.4f, nullptr, nullptr));
        return;
    }

    // Highlight target to be deleted
    animator.Push(Step(0.55f, [z](float t){
        z->highlighted = true;
        z->highlightColor = HIGHLIGHT_TARGET;
    }, nullptr));

    // We will perform deletion in tree structure but animate nodes moving:
    // Approach: perform the actual delete (modifying pointers), then reflow positions and animate movement.
    animator.Push(Step(0.02f, nullptr, [=](){
        tree.DeleteRaw(value);
    }));
    // Reflow and animate - do this in a step's onFinish so it happens after deletion
    animator.Push(Step(0.01f, nullptr, [=](){
        AnimateReflow(0.8f);
    }));
}

// Traversal animation: visit nodes in order and highlight them one by one
void AnimateTraversal(const string &which) {
    if (animator.IsBusy()) return;
    vector<shared_ptr<TreeNode>> order;
    if (which == "in") tree.InOrder(tree.root, order);
    else if (which == "pre") tree.PreOrder(tree.root, order);
    else tree.PostOrder(tree.root, order);

    for (auto &n : order) {
        animator.Push(Step(0.5f, [n](float t){
            n->highlighted = true;
            n->highlightColor = HIGHLIGHT_CUR;
        }, [n](){
            n->highlighted = false;
        }));
    }
}

// --------------------------- Misc helpers ---------------------------
// Click detection for nodes: allow clicking to highlight a node (visual only)
shared_ptr<TreeNode> GetNodeAtPoint(shared_ptr<TreeNode> n, Vector2 screenPt) {
    if (!n) return nullptr;
    // Transform screen point to world space
    Vector2 worldPt = GetScreenToWorld2D(screenPt, camera);
    
    // post-order depth-first search to prefer children closer to the click
    auto left = GetNodeAtPoint(n->left, screenPt);
    if (left) return left;
    auto right = GetNodeAtPoint(n->right, screenPt);
    if (right) return right;
    float dx = n->pos.x - worldPt.x, dy = n->pos.y - worldPt.y;
    if (dx*dx + dy*dy <= NODE_RADIUS*NODE_RADIUS) return n;
    return nullptr;
}

// --------------------------- Main ---------------------------
int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "BST Visualiser - Raylib");
    SetTargetFPS(60);

    // Initialize camera
    camera.offset = { SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
    camera.target = { 0, 0 };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    ui.Init();

    // initial sample nodes
    vector<int> seed = {50, 30, 70, 20, 40, 60, 80};
    for (int v : seed) tree.InsertRaw(v);
    tree.ComputeLayout();
    // initialize node pos & animFrom to target so they already at their places
    std::function<void(shared_ptr<TreeNode>)> initPos = [&](shared_ptr<TreeNode> n){
        if (!n) return;
        n->pos = n->targetPos;
        n->animFrom = n->pos;
        n->animT = 1.0f;
        initPos(n->left);
        initPos(n->right);
    };
    initPos(tree.root);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        // handle ui input & clicks
        ui.HandleInput();

        // handle mouse click on nodes to simply highlight
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 mp = GetMousePosition();
            // avoid clicks inside the UI input rectangle and buttons area
            if (!(CheckCollisionPointRec(mp, ui.inputRect))) {
                for (auto &b : ui.buttons) {
                    if (CheckCollisionPointRec(mp, b.rect)) goto skipNodeClick;
                }
                if (CheckCollisionPointRec(mp, ui.speedBtn.rect) || CheckCollisionPointRec(mp, ui.resetBtn.rect)) {
                    // UI
                } else {
                    auto n = GetNodeAtPoint(tree.root, mp);
                    if (n) {
                        n->highlighted = true;
                        n->highlightColor = HIGHLIGHT_CUR;
                        // schedule removal of highlight
                        animator.Push(Step(0.8f, nullptr, [n](){ n->highlighted = false; }));
                    }
                }
            }
        }
        skipNodeClick:;

        // Update animator which drives most animations
        animator.Update(dt);

        // Update any continuous node animations (keeps nodes synced)
        UpdateNodeAnimations(tree.root, dt);
        
        // Update camera to auto-zoom and fit tree
        UpdateCamera(dt);

        // Drawing
        BeginDrawing();
        ClearBackground(BG);

        // Begin camera mode for tree rendering
        BeginMode2D(camera);
        
        // draw tree edges & nodes
        if (tree.root) {
            DrawTreeNodes(tree.root);
        } else {
            // Note: This text won't be affected by camera zoom
            DrawText("Tree is empty. Insert a number to begin.", 420, 220, 20, Fade(TEXT_COLOR, 0.7f));
        }
        
        EndMode2D();

        // top UI (drawn after camera mode, so not affected by zoom)
        ui.Draw();

        // bottom panel: status
        DrawRectangle(0, SCREEN_HEIGHT - 32, SCREEN_WIDTH, 32, (Color){20,20,28,200});
        string status = "Nodes: " + ToStr(tree.Count(tree.root)) + "    Speed: x" + ToStr((int)round(globalSpeed));
        DrawText(status.c_str(), 8, SCREEN_HEIGHT - 26, 14, Fade(TEXT_COLOR, 0.9f));

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
