#ifndef ISOMAPI_H
#define ISOMAPI_H
#include "../CrossCutLib/Logger.h"
#include "../MappingCoreLib/MappingCore.h"
#include <chrono>
#include <cstdint>
#include <string_view>

// This file pulls out the majority of code related to ISOM from the various places they'd otherwise be found in Chkdraft's mapping core code 

extern Logger logger;

namespace Sc {
    
    struct BoundingBox
    {
        size_t left = 0;
        size_t top = 0;
        size_t right = 0;
        size_t bottom = 0;

        constexpr BoundingBox() = default;
        constexpr BoundingBox(size_t left, size_t top, size_t right, size_t bottom)
            : left(left), top(top), right(right), bottom(bottom) {}
        constexpr BoundingBox(size_t oldWidth, size_t oldHeight, size_t newWidth, size_t newHeight, long xOffset, long yOffset) :
            left(xOffset > 0 ? 0 : -xOffset),
            top(yOffset > 0 ? 0 : -yOffset),
            right(oldWidth - left > newWidth ? left + newWidth : oldWidth),
            bottom(oldHeight - top > newHeight ? top + newHeight : oldHeight) {}

        constexpr void expandToInclude(size_t x, size_t y) {
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    };

    namespace Isom
    {
        enum class Link : uint16_t {
            None = 0, // No-link

            // Soft-links range from 1 to 48
            SoftLinks = 48,
            HardLinks = 48,

            // Anything over 48 is a hard link which is used in identifying shape quadrants and linking entries within the same terrain type

            BL = 49, // Bottom-left link
            TR = 50, // Top-right link
            BR = 51, // Bottom-right link
            TL = 52, // Top-left link
            FR = 53, // Far-right link
            FL = 54, // Far-left link
            LH = 55, // Left-hand side link
            RH = 56 // Right-hand side link
        };

        // LinkIds are a singular number for comparing values in the isomLink table (instead of the four directional links); some linkIds have special meaning
        enum class LinkId : uint16_t {
            None = 0, // In shapes a LinkId of "none" implies a linkId that needs to be populated (after calculating shapes and directional link values)

            // LinkId values greater than 0 but less than 255 allow for matches with tiles outside of the same terrain type

            // The special LinkId values (255 or higher) are only used for matches within the same terrain type

            TRBL_NW = 255, // A hardcoded top-right and/or bottom-left linkId used on shapes found towards the north-west of terrain types
            TRBL_SE = 256, // A hardcoded top-right and/or bottom-left linkId used on shapes found towards the south-east of terrain types
            TLBR_NE = 257, // A hardcoded top-left and/or bottom-right linkId used on shapes found towards the north-east of terrain types
            TLBR_SW = 258, // A hardcoded top-left and/or bottom-right linkId used on shapes found towards the south-west of terrain types

            OnlyMatchSameType = TRBL_NW // One of the hardcoded values (255 or higher) implies a match can only be made within the same terrain type
        };

        #pragma pack(push, 1)
        __declspec(align(1)) struct DirectionalLinks
        {
            Link left = Link::None;
            Link top = Link::None;
            Link right = Link::None;
            Link bottom = Link::None;

            constexpr bool hasNoHardLinks() const { // A CV5 entry that has no hard links does not participate in the creation of the isomLink table
                return left <= Link::SoftLinks && top <= Link::SoftLinks && right <= Link::SoftLinks && bottom <= Link::SoftLinks;
            }

            constexpr bool isAllHardLinks() const { // A CV5 entry that is all hard links does not participate in the creation of the isomLink table
                return left > Link::SoftLinks && top > Link::SoftLinks && right > Link::SoftLinks && bottom > Link::SoftLinks;
            }

            constexpr bool isShapeQuadrant() const { // A CV5 entry with no hard links or that is all hard links is excluded from the isomLink table
                return !isAllHardLinks() && !hasNoHardLinks();
            }
        };
        __declspec(align(1)) struct TileGroup {
            uint16_t terrainType;
            uint8_t buildability;
            uint8_t groundHeight;
            DirectionalLinks links;
            Rect stackConnections;
            uint16_t megaTileIndex[16]; // megaTileIndex - to VF4/VX4
        };
        #pragma pack(pop)
        
        struct Side_ { // A side of a rectangle
            enum uint16_t_ : uint16_t {
                Left = 0,
                Top = 1,
                Right = 2,
                Bottom = 3,

                Total
            };
        };
        using Side = Side_::uint16_t_;
        static constexpr Side sides[Side::Total] { Side::Left, Side::Top, Side::Right, Side::Bottom };

        enum class Quadrant
        {
            TopLeft,
            TopRight,
            BottomRight,
            BottomLeft
        };

        static constexpr Quadrant quadrants[] { Quadrant::TopLeft, Quadrant::TopRight, Quadrant::BottomRight, Quadrant::BottomLeft };

        static constexpr Quadrant OppositeQuadrant(size_t i) {
            switch ( Quadrant(i) ) {
                case Quadrant::TopLeft: return Quadrant::BottomRight;
                case Quadrant::TopRight: return Quadrant::BottomLeft;
                case Quadrant::BottomRight: return Quadrant::TopLeft;
                default: /*Quadrant::BottomLeft*/ return Quadrant::TopRight;
            }
        }

        struct EdgeFlags_ {
            enum uint16_t_ : uint16_t {
                TopLeft_Right    = 0x0, // Quadrant::TopLeft, isomRect.right
                TopLeft_Bottom   = 0x2, // Quadrant::TopLeft, isomRect.bottom
                TopRight_Left    = 0x4, // Quadrant::TopRight, isomRect.left
                TopRight_Bottom  = 0x6, // Quadrant::TopRight, isomRect.bottom
                BottomRight_Left = 0x8, // Quadrant::BottomRight, isomRect.top
                BottomRight_Top  = 0xA, // Quadrant::BottomRight, isomRect.left
                BottomLeft_Top   = 0xC, // Quadrant::BottomLeft, isomRect.top
                BottomLeft_Right = 0xE, // Quadrant::BottomLeft, isomRect.right

                Mask = 0xE
            };
        };
        using EdgeFlags = EdgeFlags_::uint16_t_;

        struct ProjectedQuadrant { // The 8x4 rectangle a diamond projects onto has four quadrants, each consisting of two sides of an IsomRect
            Side firstSide; // An isom rect side; note: the first side should always be before second in rect-normal order: left, top, right, bottom
            Side secondSide; // An isom rect side; note: the second side should always be after first in rect-normal order: left, top, right, bottom
            EdgeFlags firstEdgeFlags; // The edge flags that get associated with the "firstSide" of the isom rect
            EdgeFlags secondEdgeFlags; // The edge flags that get associated with the "secondSide" of the isom rect

            constexpr ProjectedQuadrant(Side firstSide, Side secondSide, EdgeFlags firstEdgeFlags, EdgeFlags secondEdgeFlags)
                : firstSide(firstSide), secondSide(secondSide), firstEdgeFlags(firstEdgeFlags), secondEdgeFlags(secondEdgeFlags) {}

            static constexpr ProjectedQuadrant at(Quadrant quadrant) {
                switch ( quadrant ) {
                    case Quadrant::TopLeft: return { Side::Right, Side::Bottom, EdgeFlags::TopLeft_Right, EdgeFlags::TopLeft_Bottom };
                    case Quadrant::TopRight: return { Side::Left, Side::Bottom, EdgeFlags::TopRight_Left, EdgeFlags::TopRight_Bottom };
                    case Quadrant::BottomRight: return { Side::Left, Side::Top, EdgeFlags::BottomRight_Left, EdgeFlags::BottomRight_Top };
                    default: /*Quadrant::BottomLeft*/ return { Side::Top, Side::Right, EdgeFlags::BottomLeft_Top, EdgeFlags::BottomLeft_Right };
                }
            }

            constexpr ProjectedQuadrant(Quadrant quadrant) : ProjectedQuadrant(ProjectedQuadrant::at(quadrant)) {}
        };

        struct ShapeLinks
        {
            struct TopLeftQuadrant {
                Link right = Link::None;
                Link bottom = Link::None;
                LinkId linkId = LinkId::None;
            };

            struct TopRightQuadrant {
                Link left = Link::None;
                Link bottom = Link::None;
                LinkId linkId = LinkId::None;
            };

            struct BottomRightQuadrant {
                Link left = Link::None;
                Link top = Link::None;
                LinkId linkId = LinkId::None;
            };

            struct BottomLeftQuadrant {
                Link top = Link::None;
                Link right = Link::None;
                LinkId linkId = LinkId::None;
            };

            uint8_t terrainType = 0;
            TopLeftQuadrant topLeft {};
            TopRightQuadrant topRight {};
            BottomRightQuadrant bottomRight {};
            BottomLeftQuadrant bottomLeft {};

            constexpr LinkId getLinkId(Quadrant quadrant) const {
                switch ( quadrant )
                {
                    case Quadrant::TopLeft: return topLeft.linkId;
                    case Quadrant::TopRight: return topRight.linkId;
                    case Quadrant::BottomRight: return bottomRight.linkId;
                    default: /*Quadrant::BottomLeft*/ return bottomLeft.linkId;
                }
            }

            constexpr Link getEdgeLink(uint16_t isomValue) const {
                switch ( isomValue & EdgeFlags::Mask ) {
                    case EdgeFlags::TopLeft_Right: return topLeft.right;
                    case EdgeFlags::TopLeft_Bottom: return topLeft.bottom;
                    case EdgeFlags::TopRight_Left: return topRight.left;
                    case EdgeFlags::TopRight_Bottom: return topRight.bottom;
                    case EdgeFlags::BottomRight_Left: return bottomRight.left;
                    case EdgeFlags::BottomRight_Top: return bottomRight.top;
                    case EdgeFlags::BottomLeft_Top: return bottomLeft.top;
                    default: /*EdgeFlags::BottomLeft_Right*/ return bottomLeft.right;
                }
            }
        };

        struct ShapeQuadrant
        {
            Link left = Link::None;
            Link top = Link::None;
            Link right = Link::None;
            Link bottom = Link::None;
            LinkId linkId = LinkId::None;
            bool isStackTop = false;

            constexpr bool matches(const DirectionalLinks & links, bool noStackAbove) const
            {
                return
                    (links.left == left || (links.left <= Link::SoftLinks && left <= Link::SoftLinks)) && // If either is a hard link, the values must match
                    (links.top == top || (links.top <= Link::SoftLinks && top <= Link::SoftLinks)) &&
                    (links.right == right || (links.right <= Link::SoftLinks && right <= Link::SoftLinks)) &&
                    (links.bottom == bottom || (links.bottom <= Link::SoftLinks && bottom <= Link::SoftLinks)) &&
                    (noStackAbove || !isStackTop); // Either no groups are stacked above this one... or this shape quadrant isn't at stack top
            }

            constexpr ShapeQuadrant & setLeft(Link left) {
                this->left = left;
                return *this;
            }
            constexpr ShapeQuadrant & setTop(Link top) {
                this->top = top;
                return *this;
            }
            constexpr ShapeQuadrant & setRight(Link right) {
                this->right = right;
                return *this;
            }
            constexpr ShapeQuadrant & setBottom(Link bottom) {
                this->bottom = bottom;
                return *this;
            }
            constexpr ShapeQuadrant & setLinkId(LinkId linkId) {
                this->linkId = linkId;
                return *this;
            }
            constexpr ShapeQuadrant & setIsStackTop() {
                this->isStackTop = true;
                return *this;
            }
        };

        struct Shape
        {
            enum Id : size_t {
                EdgeNorthWest, EdgeNorthEast, EdgeSouthEast, EdgeSouthWest,
                JutOutNorth, JutOutEast, JutOutSouth, JutOutWest,
                JutInEast, JutInWest, JutInNorth, JutInSouth,
                Horizontal, Vertical
            };

            ShapeQuadrant topLeft {};
            ShapeQuadrant topRight {};
            ShapeQuadrant bottomRight {};
            ShapeQuadrant bottomLeft {};

            constexpr bool matches(Quadrant quadrant, const DirectionalLinks & links, bool noStackAbove) const
            {
                switch ( quadrant )
                {
                    case Quadrant::TopLeft: return topLeft.matches(links, noStackAbove);
                    case Quadrant::TopRight: return topRight.matches(links, noStackAbove);
                    case Quadrant::BottomRight: return bottomRight.matches(links, noStackAbove);
                    default: /*Quadrant::BottomLeft*/ return bottomLeft.matches(links, noStackAbove);
                }
            }

            constexpr Shape & setTopLeft(ShapeQuadrant topLeft) {
                this->topLeft = topLeft;
                return *this;
            }
            constexpr Shape & setTopRight(ShapeQuadrant topRight) {
                this->topRight = topRight;
                return *this;
            }
            constexpr Shape & setBottomRight(ShapeQuadrant bottomRight) {
                this->bottomRight = bottomRight;
                return *this;
            }
            constexpr Shape & setBottomLeft(ShapeQuadrant bottomLeft) {
                this->bottomLeft = bottomLeft;
                return *this;
            }
        };

        struct ShapeDefinitions
        {
            static constexpr Shape edgeNorthWest = Shape{} // 0
                .setTopRight(ShapeQuadrant{}.setRight(Link::BR).setBottom(Link::BR).setLinkId(LinkId::TRBL_NW).setIsStackTop())
                .setBottomRight(ShapeQuadrant{}.setLeft(Link::BR).setTop(Link::BR))
                .setBottomLeft(ShapeQuadrant{}.setRight(Link::BR).setBottom(Link::FR).setLinkId(LinkId::TRBL_NW).setIsStackTop());

            static constexpr Shape edgeNorthEast = Shape{} // 1
                .setTopLeft(ShapeQuadrant{}.setLeft(Link::BL).setBottom(Link::BL).setLinkId(LinkId::TLBR_NE).setIsStackTop())
                .setBottomRight(ShapeQuadrant{}.setLeft(Link::BL).setBottom(Link::FL).setLinkId(LinkId::TLBR_NE).setIsStackTop())
                .setBottomLeft(ShapeQuadrant{}.setTop(Link::BL).setRight(Link::BL));

            static constexpr Shape edgeSouthEast = Shape{} // 2
                .setTopLeft(ShapeQuadrant{}.setRight(Link::TL).setBottom(Link::TL))
                .setTopRight(ShapeQuadrant{}.setLeft(Link::TL).setTop(Link::FL).setLinkId(LinkId::TRBL_SE))
                .setBottomLeft(ShapeQuadrant{}.setLeft(Link::TL).setTop(Link::TL).setLinkId(LinkId::TRBL_SE));

            static constexpr Shape edgeSouthWest = Shape{} // 3
                .setTopLeft(ShapeQuadrant{}.setTop(Link::FR).setRight(Link::TR).setLinkId(LinkId::TLBR_SW))
                .setTopRight(ShapeQuadrant{}.setLeft(Link::TR).setBottom(Link::TR))
                .setBottomRight(ShapeQuadrant{}.setTop(Link::TR).setRight(Link::TR).setLinkId(LinkId::TLBR_SW));

            static constexpr Shape jutOutNorth = Shape{} // 4
                .setBottomRight(ShapeQuadrant{}.setLeft(Link::BL).setBottom(Link::BL).setLinkId(LinkId::TLBR_NE).setIsStackTop())
                .setBottomLeft(ShapeQuadrant{}.setRight(Link::BR).setBottom(Link::BR).setLinkId(LinkId::TRBL_NW).setIsStackTop());

            static constexpr Shape jutOutEast = Shape{} // 5
                .setTopLeft(ShapeQuadrant{}.setLeft(Link::BL).setBottom(Link::FL).setLinkId(LinkId::TLBR_NE).setIsStackTop())
                .setBottomLeft(ShapeQuadrant{}.setLeft(Link::TL).setTop(Link::FL).setLinkId(LinkId::TRBL_SE));

            static constexpr Shape jutOutSouth = Shape{} // 6
                .setTopLeft(ShapeQuadrant{}.setTop(Link::TR).setRight(Link::TR).setLinkId(LinkId::TLBR_SW))
                .setTopRight(ShapeQuadrant{}.setLeft(Link::TL).setTop(Link::TL).setLinkId(LinkId::TRBL_SE));

            static constexpr Shape jutOutWest = Shape{} // 7
                .setTopRight(ShapeQuadrant{}.setRight(Link::BR).setBottom(Link::FR).setLinkId(LinkId::TRBL_NW).setIsStackTop())
                .setBottomRight(ShapeQuadrant{}.setTop(Link::FR).setRight(Link::TR).setLinkId(LinkId::TLBR_SW));

            static constexpr Shape jutInEast = Shape{} // 8
                .setTopLeft(ShapeQuadrant{}.setTop(Link::FR).setRight(Link::TR).setLinkId(LinkId::TLBR_SW))
                .setTopRight(ShapeQuadrant{}.setLeft(Link::RH).setBottom(Link::RH))
                .setBottomRight(ShapeQuadrant{}.setLeft(Link::RH).setTop(Link::RH))
                .setBottomLeft(ShapeQuadrant{}.setRight(Link::BR).setBottom(Link::FR).setLinkId(LinkId::TRBL_NW));

            static constexpr Shape jutInWest = Shape{} // 9
                .setTopLeft(ShapeQuadrant{}.setRight(Link::LH).setBottom(Link::LH))
                .setTopRight(ShapeQuadrant{}.setLeft(Link::TL).setTop(Link::FL).setLinkId(LinkId::TRBL_SE))
                .setBottomRight(ShapeQuadrant{}.setLeft(Link::BL).setBottom(Link::FL).setLinkId(LinkId::TLBR_NE))
                .setBottomLeft(ShapeQuadrant{}.setTop(Link::LH).setRight(Link::LH));

            static constexpr Shape jutInNorth = Shape{} // 10
                .setTopLeft(ShapeQuadrant{}.setLeft(Link::BL).setBottom(Link::BL).setLinkId(LinkId::TLBR_NE).setIsStackTop())
                .setTopRight(ShapeQuadrant{}.setRight(Link::BR).setBottom(Link::BR).setLinkId(LinkId::TRBL_NW).setIsStackTop())
                .setBottomRight(ShapeQuadrant{}.setLeft(Link::BR).setTop(Link::BR))
                .setBottomLeft(ShapeQuadrant{}.setTop(Link::BL).setRight(Link::BL));

            static constexpr Shape jutInSouth = Shape{} // 11
                .setTopLeft(ShapeQuadrant{}.setRight(Link::TL).setBottom(Link::TL))
                .setTopRight(ShapeQuadrant{}.setLeft(Link::TR).setBottom(Link::TR))
                .setBottomRight(ShapeQuadrant{}.setTop(Link::TR).setRight(Link::TR).setLinkId(LinkId::TLBR_SW))
                .setBottomLeft(ShapeQuadrant{}.setLeft(Link::TL).setTop(Link::TL).setLinkId(LinkId::TRBL_SE));

            static constexpr Shape horizontal = Shape{} // 12
                .setTopLeft(ShapeQuadrant{}.setTop(Link::TR).setRight(Link::TR).setLinkId(LinkId::TLBR_SW))
                .setTopRight(ShapeQuadrant{}.setLeft(Link::TL).setTop(Link::TL).setLinkId(LinkId::TRBL_SE))
                .setBottomRight(ShapeQuadrant{}.setLeft(Link::BL).setBottom(Link::BL).setLinkId(LinkId::TLBR_NE))
                .setBottomLeft(ShapeQuadrant{}.setRight(Link::BR).setBottom(Link::BR).setLinkId(LinkId::TRBL_NW));

            static constexpr Shape vertical = Shape{} // 13
                .setTopLeft(ShapeQuadrant{}.setLeft(Link::BL).setBottom(Link::FL).setLinkId(LinkId::TLBR_NE))
                .setTopRight(ShapeQuadrant{}.setRight(Link::BR).setBottom(Link::FR).setLinkId(LinkId::TRBL_NW))
                .setBottomRight(ShapeQuadrant{}.setTop(Link::FR).setRight(Link::TR).setLinkId(LinkId::TLBR_SW))
                .setBottomLeft(ShapeQuadrant{}.setLeft(Link::TL).setTop(Link::FL).setLinkId(LinkId::TRBL_SE));

            static constexpr Shape shapes[] {
                edgeNorthWest, edgeNorthEast, edgeSouthEast, edgeSouthWest,
                jutOutNorth, jutOutEast, jutOutSouth, jutOutWest,
                jutInEast, jutInWest, jutInNorth, jutInSouth,
                horizontal, vertical
            };
        };
        static constexpr Span<Shape> shapes = ShapeDefinitions::shapes;

        struct ShapeTileGroup { // Used to record the tileGroup indexes which are used to populate the quadrants in different shapes
            uint16_t topLeft = std::numeric_limits<uint16_t>::max();
            uint16_t topRight = std::numeric_limits<uint16_t>::max();
            uint16_t bottomRight = std::numeric_limits<uint16_t>::max();
            uint16_t bottomLeft = std::numeric_limits<uint16_t>::max();
        };

        struct TerrainTypeShapes // Every terrain type has 14 shapes associated with it
        {
            ShapeLinks edgeNorthWest;
            ShapeLinks edgeNorthEast;
            ShapeLinks edgeSouthEast;
            ShapeLinks edgeSouthWest;
            ShapeLinks jutOutNorth;
            ShapeLinks jutOutEast;
            ShapeLinks jutOutSouth;
            ShapeLinks jutOutWest;
            ShapeLinks jutInEast;
            ShapeLinks jutInWest;
            ShapeLinks jutInNorth;
            ShapeLinks jutInSouth;
            ShapeLinks horizontal;
            ShapeLinks vertical;

            constexpr ShapeLinks & operator[](size_t i) {
                switch ( Shape::Id(i) )
                {
                    case Shape::Id::EdgeNorthWest: return edgeNorthWest;
                    case Shape::Id::EdgeNorthEast: return edgeNorthEast;
                    case Shape::Id::EdgeSouthEast: return edgeSouthEast;
                    case Shape::Id::EdgeSouthWest: return edgeSouthWest;
                    case Shape::Id::JutOutNorth: return jutOutNorth;
                    case Shape::Id::JutOutEast: return jutOutEast;
                    case Shape::Id::JutOutSouth: return jutOutSouth;
                    case Shape::Id::JutOutWest: return jutOutWest;
                    case Shape::Id::JutInEast: return jutInEast;
                    case Shape::Id::JutInWest: return jutInWest;
                    case Shape::Id::JutInNorth: return jutInNorth;
                    case Shape::Id::JutInSouth: return jutInSouth;
                    case Shape::Id::Horizontal: return horizontal;
                    default: /*Shape::Id::Vertical*/ return vertical;
                }
            }

            // Terrain types like rocky ground exclude JutInE/JutInW far right/left side CV5 entries, they are instead populated using nearby shapes
            inline void populateJutInEastWest(Span<Sc::Isom::TileGroup> tilesetCv5s, Span<ShapeTileGroup> shapeTileGroups)
            {
                // The right sides of JutInE are not always present in CV5, when missing they're filled by a merge of EdgeNe/EdgeSe
                if ( jutInEast.topRight.left == Link::None )
                {
                    jutInEast.topRight.left = tilesetCv5s[shapeTileGroups[Shape::EdgeNorthEast].bottomLeft].links.left;
                    jutInEast.topRight.bottom = tilesetCv5s[shapeTileGroups[Shape::EdgeNorthEast].bottomLeft].links.bottom;
                    jutInEast.bottomRight.left = tilesetCv5s[shapeTileGroups[Shape::EdgeSouthEast].topLeft].links.left;
                    jutInEast.bottomRight.top = tilesetCv5s[shapeTileGroups[Shape::EdgeSouthEast].topLeft].links.top;
                }

                // The left sides of JutInW are not always present in CV5, when missing they're filled in by a merge of EdgeNw/EdgeSw
                if ( jutInWest.topLeft.right == Link::None )
                {
                    jutInWest.topLeft.right = tilesetCv5s[shapeTileGroups[Shape::EdgeNorthWest].bottomRight].links.right;
                    jutInWest.topLeft.bottom = tilesetCv5s[shapeTileGroups[Shape::EdgeNorthWest].bottomRight].links.bottom;
                    jutInWest.bottomLeft.top = tilesetCv5s[shapeTileGroups[Shape::EdgeSouthWest].topRight].links.top;
                    jutInWest.bottomLeft.right = tilesetCv5s[shapeTileGroups[Shape::EdgeSouthWest].topRight].links.right;
                }
            }

            // Populate the links in quadrants that are not part of the primary shape using adjacent link values
            inline void populateEmptyQuadrantLinks()
            {
                edgeNorthWest.topLeft.right = edgeNorthWest.topRight.left;
                edgeNorthWest.topLeft.bottom = edgeNorthWest.bottomLeft.top;

                edgeNorthEast.topRight.left = edgeNorthEast.topLeft.right;
                edgeNorthEast.topRight.bottom = edgeNorthEast.bottomRight.top;

                edgeSouthEast.bottomRight.left = edgeSouthEast.bottomLeft.right;
                edgeSouthEast.bottomRight.top = edgeSouthEast.topRight.bottom;

                edgeSouthWest.bottomLeft.top = edgeSouthWest.topLeft.bottom;
                edgeSouthWest.bottomLeft.right = edgeSouthWest.bottomRight.left;

                jutOutNorth.topLeft.bottom = jutOutNorth.bottomLeft.top;
                jutOutNorth.topLeft.right = jutOutNorth.topLeft.bottom;
                jutOutNorth.topRight.bottom = jutOutNorth.bottomRight.top;
                jutOutNorth.topRight.left = jutOutNorth.topRight.bottom;

                auto fillLink = jutOutEast.topLeft.right;
                jutOutEast.topRight.left = fillLink;
                jutOutEast.topRight.bottom = fillLink;
                jutOutEast.bottomRight.left = fillLink;
                jutOutEast.bottomRight.top = fillLink;
                
                jutOutSouth.bottomRight.top = jutOutSouth.topRight.bottom;
                jutOutSouth.bottomRight.left = jutOutSouth.bottomRight.top;
                jutOutSouth.bottomLeft.top = jutOutSouth.topLeft.bottom;
                jutOutSouth.bottomLeft.right = jutOutSouth.bottomLeft.top;

                fillLink = jutOutWest.topRight.left;
                jutOutWest.topLeft.right = fillLink;
                jutOutWest.topLeft.bottom = fillLink;
                jutOutWest.bottomLeft.right = fillLink;
                jutOutWest.bottomLeft.top = fillLink;
            }

            // Fill in the hardcoded linkIds (which are always the same for the set of 14 shapes making up one terrain type)
            inline void populateHardcodedLinkIds()
            {
                for ( size_t shapeIndex=0; shapeIndex<shapes.size(); ++shapeIndex )
                {
                    const auto & shape = shapes[shapeIndex];
                    if ( shape.topLeft.linkId >= LinkId::OnlyMatchSameType )
                        (*this)[shapeIndex].topLeft.linkId = shape.topLeft.linkId;
                    if ( shape.topRight.linkId >= LinkId::OnlyMatchSameType )
                        (*this)[shapeIndex].topRight.linkId = shape.topRight.linkId;
                    if ( shape.bottomRight.linkId >= LinkId::OnlyMatchSameType )
                        (*this)[shapeIndex].bottomRight.linkId = shape.bottomRight.linkId;
                    if ( shape.bottomLeft.linkId >= LinkId::OnlyMatchSameType )
                        (*this)[shapeIndex].bottomLeft.linkId = shape.bottomLeft.linkId;
                }
            }

            inline void fillOuterLinkIds(LinkId linkId)
            {
                edgeNorthWest.topLeft.linkId = linkId;
                
                edgeNorthEast.topRight.linkId = linkId;
                
                edgeSouthEast.bottomRight.linkId = linkId;
                
                edgeSouthWest.bottomLeft.linkId = linkId;
                
                jutOutNorth.topLeft.linkId = linkId;
                jutOutNorth.topRight.linkId = linkId;
                
                jutOutEast.topRight.linkId = linkId;
                jutOutEast.bottomRight.linkId = linkId;
                
                jutOutWest.topLeft.linkId = linkId;
                jutOutWest.bottomLeft.linkId = linkId;
                
                jutOutSouth.bottomRight.linkId = linkId;
                jutOutSouth.bottomLeft.linkId = linkId;
            }

            inline void fillInnerLinkIds(LinkId linkId)
            {
                edgeNorthWest.bottomRight.linkId = linkId;
                
                edgeNorthEast.bottomLeft.linkId = linkId;
                
                edgeSouthEast.topLeft.linkId = linkId;
                
                edgeSouthWest.topRight.linkId = linkId;
                
                jutInEast.topRight.linkId = linkId;
                jutInEast.bottomRight.linkId = linkId;
                
                jutInWest.topLeft.linkId = linkId;
                jutInWest.bottomLeft.linkId = linkId;
                
                jutInNorth.bottomRight.linkId = linkId;
                jutInNorth.bottomLeft.linkId = linkId;
                
                jutInSouth.topLeft.linkId = linkId;
                jutInSouth.topRight.linkId = linkId;
            }

            inline void populateLinkIdsToSolidBrushes(Span<Sc::Isom::TileGroup> tilesetCv5s, Span<ShapeTileGroup> shapeTileGroups,
                size_t totalSolidBrushEntries, const std::vector<ShapeLinks> & isomLinks)
            {
                // Using completed edge links, lookup and fill in the linkIds to the solid brushes
                for ( size_t i=0; i<totalSolidBrushEntries; ++i )
                {
                    auto brushLink = isomLinks[i].topLeft.right; // Arbitrary quadrant/direction since links/ids are all the same across a given solid brush
                    auto brushLinkId = isomLinks[i].topLeft.linkId;

                    if ( brushLink == tilesetCv5s[shapeTileGroups[Shape::EdgeNorthWest].topRight].links.left ) // Found the outer solid brush
                        fillOuterLinkIds(brushLinkId);

                    if ( brushLink == tilesetCv5s[shapeTileGroups[Shape::EdgeNorthWest].bottomRight].links.right ) // Found the inner solid brush
                        fillInnerLinkIds(brushLinkId);
                }
            }


        };

        struct TerrainTypeInfo
        {
            uint16_t index = 0;
            uint16_t isomValue = 0; // This is both the value placed in the ISOM section and an index into the isomLink table
            int16_t brushSortOrder = -1;
            LinkId linkId = LinkId::None; // The linkId column in the isomLink table (not an index into the table)
            std::string_view name = "";
        };

        struct Brush
        {
            struct Badlands
            {
                enum : size_t {
                    Dirt = 2,
                    Mud = 4,
                    HighDirt = 3,
                    Water = 5,
                    Grass = 6,
                    HighGrass = 7,
                    Structure = 18,
                    Asphalt = 14,
                    RockyGround = 15,

                    Default = Dirt
                };

                static constexpr TerrainTypeInfo terrainTypeInfo[] {
                    { 0,           10                                }, {1},
                    { Dirt,         1, 0, LinkId( 1), "Dirt"         },
                    { HighDirt,     2, 2, LinkId( 2), "High Dirt"    },
                    { Mud,          9, 1, LinkId( 4), "Mud"          },
                    { Water,        3, 3, LinkId( 3), "Water"        },
                    { Grass,        4, 4, LinkId( 5), "Grass"        },
                    { HighGrass,    7, 5, LinkId( 6), "High Grass"   }, {8}, {9}, {10}, {11}, {12}, {13},
                    { Asphalt,      5, 7, LinkId( 9), "Asphalt"      },
                    { RockyGround,  6, 8, LinkId(10), "Rocky Ground" }, {16}, {17},
                    { Structure,    8, 6, LinkId( 7), "Structure"    },

                    { 19,   0 },
                    { 20,  41 },
                    { 21,  69 },
                    { 22, 111 }, {23}, {24}, {25}, {26},
                    { 27,  83 },
                    { 28,  55 }, {29}, {30},
                    { 31,  97 }, {32}, {33},
                    { 34,  13 },
                    { 35,  27 }
                };

                static constexpr uint16_t terrainTypeMap[] {
                    5, 35, 0,
                    35, 5, 2, 20, 27, 28, 34, 22, 0,
                    2, 34, 35, 20, 27, 28, 22, 0,
                    34, 2, 3, 20, 21, 27, 28, 35, 22, 0,
                    3, 34, 21, 0,
                    6, 20, 0,
                    20, 6, 2, 35, 34, 27, 28, 22, 0,
                    14, 27, 31, 0,
                    27, 14, 20, 2, 35, 34, 28, 22, 0,
                    15, 28, 0,
                    28, 15, 2, 34, 35, 20, 27, 22, 0,
                    7, 21, 0,
                    21, 7, 3, 34, 0,
                    18, 31, 0,
                    31, 18, 14, 0,
                    4, 22, 0,
                    22, 4, 2, 34, 35, 20, 27, 28, 0,
                    0
                };
            };

            struct Space
            {
                enum : size_t {
                    Space_ = 2,
                    LowPlatform = 8,
                    RustyPit = 9,
                    Platform = 3,
                    DarkPlatform = 11,
                    Plating = 4,
                    SolarArray = 7,
                    HighPlatform = 5,
                    HighPlating = 6,
                    ElevatedCatwalk = 10,

                    Default = Platform
                };

                static constexpr TerrainTypeInfo terrainTypeInfo[] {
                    { 0,                3                                    }, {1},
                    { Space_,           1, 0, LinkId( 1), "Space"            },
                    { Platform,         2, 3, LinkId( 3), "Platform"         },
                    { Plating,         11, 5, LinkId( 4), "Plating"          },
                    { HighPlatform,     4, 7, LinkId( 5), "High Platform"    },
                    { HighPlating,     12, 8, LinkId( 6), "High Plating"     },
                    { SolarArray,       8, 6, LinkId( 7), "Solar Array"      },
                    { LowPlatform,      9, 1, LinkId( 8), "Low Platform"     },
                    { RustyPit,        10, 2, LinkId( 9), "Rusty Pit"        },
                    { ElevatedCatwalk, 13, 9, LinkId(10), "Elevated Catwalk" },
                    { DarkPlatform,    14, 4, LinkId( 2), "Dark Platform"    },

                    { 12,   0 },
                    { 13, 136 },
                    { 14,  94 },
                    { 15, 108 },
                    { 16,  52 },
                    { 17,  66 },
                    { 18,  80 },
                    { 19, 122 },
                    { 20,  24 },
                    { 21,  38 }
                };

                static constexpr uint16_t terrainTypeMap[] {
                    2, 20, 0,
                    20, 2, 3, 16, 14, 21, 13, 0,
                    3, 20, 21, 16, 17, 18, 14, 19, 13, 0,
                    21, 3, 5, 14, 16, 15, 19, 20, 17, 13, 0,
                    5, 21, 15, 0,
                    7, 16, 0,
                    16, 7, 3, 20, 21, 17, 18, 14, 19, 13, 0,
                    8, 17, 0,
                    17, 8, 3, 16, 14, 21, 13, 0,
                    9, 18, 0,
                    18, 9, 3, 16, 14, 13, 0,
                    4, 14, 0,
                    14, 4, 3, 20, 21, 16, 17, 18, 19, 13, 0,
                    6, 15, 0,
                    15, 6, 5, 21, 0,
                    10, 19, 0,
                    19, 10, 3, 16, 14, 21, 13, 0,
                    11, 13, 0,
                    13, 11, 3, 20, 21, 16, 17, 18, 14, 19, 0,
                    0
                };
            };

            struct Installation
            {
                enum : size_t {
                    Substructure = 2,
                    Floor = 3,
                    Roof = 6,
                    SubstructurePlating = 4,
                    Plating = 5,
                    SubstructurePanels = 8,
                    BottomlessPit = 7,

                    Default = Floor
                };

                static constexpr TerrainTypeInfo terrainTypeInfo[] {
                    { 0,                   8                                       }, {1},
                    { Substructure,        1, 0, LinkId(1), "Substructure"         },
                    { Floor,               2, 1, LinkId(2), "Floor"                },
                    { SubstructurePlating, 4, 3, LinkId(4), "Substructure Plating" },
                    { Plating,             5, 4, LinkId(5), "Plating"              },
                    { Roof,                3, 2, LinkId(3), "Roof"                 },
                    { BottomlessPit,       7, 6, LinkId(7), "Bottomless Pit"       },
                    { SubstructurePanels,  6, 5, LinkId(6), "Substructure Panels"  },

                    {  9,  0 },
                    { 10, 50 },
                    { 11, 64 },
                    { 12, 22 },
                    { 13, 36 },
                    { 14, 78 },
                    { 15, 92 }
                };

                static constexpr uint16_t terrainTypeMap[] {
                    2, 12, 10, 14, 15, 0,
                    12, 2, 3, 10, 11, 13, 14, 15, 0,
                    3, 12, 13, 11, 0,
                    13, 6, 3, 11, 12, 0,
                    6, 13, 0,
                    4, 10, 0,
                    10, 4, 2, 12, 14, 15, 0,
                    5, 11, 0,
                    11, 5, 3, 12, 13, 0,
                    8, 14, 0,
                    14, 8, 2, 12, 10, 15, 0,
                    7, 15, 0,
                    15, 7, 2, 12, 10, 14, 0,
                    0
                };
            };

            struct Ashworld
            {
                enum : size_t {
                    Magma = 8,
                    Dirt = 2,
                    Lava = 3,
                    Shale = 6,
                    BrokenRock = 9,
                    HighDirt = 4,
                    HighLava = 5,
                    HighShale = 7,

                    Default = Dirt
                };

                static constexpr TerrainTypeInfo terrainTypeInfo[] {
                    { 0,          9                              }, {1},
                    { Dirt,       2, 1, LinkId(2), "Dirt"        },
                    { Lava,       3, 2, LinkId(3), "Lava"        },
                    { HighDirt,   5, 5, LinkId(5), "High Dirt"   },
                    { HighLava,   6, 6, LinkId(6), "High Lava"   },
                    { Shale,      4, 3, LinkId(4), "Shale"       },
                    { HighShale,  7, 7, LinkId(7), "High Shale"  },
                    { Magma,      1, 0, LinkId(1), "Magma"       },
                    { BrokenRock, 8, 4, LinkId(8), "Broken Rock" },

                    { 10,   0 },
                    { 11,  55 },
                    { 12,  69 },
                    { 13,  83 },
                    { 14,  97 },
                    { 15, 111 },
                    { 16,  41 },
                    { 17,  27 }
                };
                
                static constexpr uint16_t terrainTypeMap[] {
                    8, 17, 0,
                    17, 8, 2, 11, 13, 16, 15, 0,
                    2, 17, 16, 11, 13, 15, 0,
                    3, 11, 0,
                    11, 3, 2, 17, 16, 13, 15, 0,
                    6, 13, 0,
                    13, 6, 2, 17, 16, 11, 15, 0,
                    9, 15, 0,
                    15, 9, 13, 2, 17, 16, 11, 0,
                    16, 2, 4, 11, 13, 12, 14, 17, 15, 0,
                    4, 16, 12, 14, 0,
                    5, 12, 0,
                    12, 5, 4, 16, 14, 0,
                    7, 14, 0,
                    14, 7, 4, 16, 12, 0,
                    0
                };
            };

            struct Jungle
            {
                enum : size_t {
                    Water = 5,
                    Dirt = 2,
                    Mud = 4,
                    Jungle_ = 8,
                    RockyGround = 15,
                    Ruins = 11,
                    RaisedJungle = 9,
                    Temple = 16,
                    HighDirt = 3,
                    HighJungle = 10,
                    HighRuins = 12,
                    HighRaisedJungle = 13,
                    HighTemple = 17,

                    Default = Jungle_
                };
                
                static constexpr TerrainTypeInfo terrainTypeInfo[] {
                    { 0,                14                                       }, {1},
                    { Dirt,              1,  1, LinkId( 1), "Dirt"               },
                    { HighDirt,          2,  8, LinkId( 2), "High Dirt"          },
                    { Mud,              13,  2, LinkId( 4), "Mud"                },
                    { Water,             3,  0, LinkId( 3), "Water"              }, {6}, {7},
                    { Jungle_,           4,  3, LinkId( 8), "Jungle"             },
                    { RaisedJungle,      5,  6, LinkId(11), "Raised Jungle"      },
                    { HighJungle,        9,  9, LinkId(14), "High Jungle"        },
                    { Ruins,             7,  5, LinkId(12), "Ruins"              },
                    { HighRuins,        10, 10, LinkId(15), "High Ruins"         },
                    { HighRaisedJungle, 11, 11, LinkId(16), "High Raised Jungle" }, {14},
                    { RockyGround,       6,  4, LinkId(10), "Rocky Ground"       },
                    { Temple,            8,  7, LinkId(13), "Temple"             },
                    { HighTemple,       12, 12, LinkId(17), "High Temple"        }, {18},

                    { 19,   0 }, {20}, {21},
                    { 22, 171 },
                    { 23,  45 },
                    { 24, 115 },
                    { 25,  87 },
                    { 26, 129 }, {27},
                    { 28,  59 },
                    { 29,  73 },
                    { 30, 143 }, {31},
                    { 32, 101 },
                    { 33, 157 },
                    { 34,  17 },
                    { 35,  31 }
                };

                static constexpr uint16_t terrainTypeMap[] {
                    5, 35, 0,
                    35, 5, 2, 23, 28, 34, 22, 0,
                    2, 34, 35, 23, 28, 22, 0,
                    34, 2, 3, 24, 23, 28, 35, 22, 0,
                    3, 34, 24, 0,
                    8, 23, 29, 25, 32, 0,
                    4, 22, 0,
                    22, 4, 2, 34, 35, 23, 28, 0,
                    23, 8, 2, 35, 34, 28, 25, 29, 22, 0,
                    15, 28, 0,
                    28, 15, 2, 34, 35, 23, 22, 0,
                    9, 29, 0,
                    29, 9, 8, 25, 32, 23, 0,
                    11, 25, 0,
                    25, 11, 8, 23, 29, 32, 0,
                    16, 32, 0,
                    32, 16, 8, 25, 29, 0,
                    10, 24, 26, 30, 33, 0,
                    24, 10, 3, 34, 26, 30, 0,
                    12, 26, 0,
                    26, 12, 10, 24, 30, 33, 0,
                    13, 30, 0,
                    30, 13, 10, 26, 24, 33, 0,
                    17, 33, 0,
                    33, 17, 10, 26, 30, 0,
                    0
                };
            };

            struct Desert
            {
                enum : size_t {
                    Tar = 5,
                    Dirt = 2,
                    DriedMud = 4,
                    SandDunes = 8,
                    RockyGround = 15,
                    Crags = 11,
                    SandySunkenPit = 9,
                    Compound = 16,
                    HighDirt = 3,
                    HighSandDunes = 10,
                    HighCrags = 12,
                    HighSandySunkenPit = 13,
                    HighCompound = 17,

                    Default = SandDunes
                };

                static constexpr TerrainTypeInfo terrainTypeInfo[] { // TODO: Copy jungle & update names?
                    { 0,                  14                                          }, {1},
                    { Dirt,                1,  1, LinkId( 1), "Dirt"                  },
                    { HighDirt,            2,  8, LinkId( 2), "High Dirt"             },
                    { DriedMud,           13,  2, LinkId( 4), "Dried Mud"             },
                    { Tar,                 3,  0, LinkId( 3), "Tar"                   }, {6}, {7},
                    { SandDunes,           4,  3, LinkId( 8), "Sand Dunes"            },
                    { SandySunkenPit,      5,  6, LinkId(11), "Sandy Sunken Pit"      },
                    { HighSandDunes,       9,  9, LinkId(14), "High Sand Dunes"       },
                    { Crags,               7,  5, LinkId(12), "Crags"                 },
                    { HighCrags,          10, 10, LinkId(15), "High Crags"            },
                    { HighSandySunkenPit, 11, 11, LinkId(16), "High Sandy Sunken Pit" }, {14},
                    { RockyGround,         6,  4, LinkId(10), "Rocky Ground"          },
                    { Compound,            8,  7, LinkId(13), "Compound"              },
                    { HighCompound,       12, 12, LinkId(17), "High Compound"         }, {18},

                    { 19,   0 }, {20}, {21},
                    { 22, 171 },
                    { 23,  45 },
                    { 24, 115 },
                    { 25,  87 },
                    { 26, 129 }, {27},
                    { 28,  59 },
                    { 29,  73 },
                    { 30, 143 }, {31},
                    { 32, 101 },
                    { 33, 157 },
                    { 34,  17 },
                    { 35,  31 }
                };

                static constexpr Span<uint16_t> terrainTypeMap{Jungle::terrainTypeMap};
            };

            struct Arctic
            {
                enum : size_t {
                    Ice = 5,
                    Snow = 2,
                    Moguls = 4,
                    Dirt = 8,
                    RockySnow = 15,
                    Grass = 11,
                    Water = 9,
                    Outpost = 16,
                    HighSnow = 3,
                    HighDirt = 10,
                    HighGrass = 12,
                    HighWater = 13,
                    HighOutpost = 17,

                    Default = Snow
                };

                static constexpr TerrainTypeInfo terrainTypeInfo[] { // TODO: Copy jungle & update names?
                    { 0,           14                                 }, {1},
                    { Snow,         1,  1, LinkId( 1), "Snow"         },
                    { HighSnow,     2,  8, LinkId( 2), "High Snow"    },
                    { Moguls,      13,  2, LinkId( 4), "Moguls"       },
                    { Ice,          3,  0, LinkId( 3), "Ice"          }, {6}, {7},
                    { Dirt,         4,  3, LinkId( 8), "Dirt"         },
                    { Water,        5,  6, LinkId(11), "Water"        },
                    { HighDirt,     9,  9, LinkId(14), "High Dirt"    },
                    { Grass,        7,  5, LinkId(12), "Grass"        },
                    { HighGrass,   10, 10, LinkId(15), "High Grass"   },
                    { HighWater,   11, 11, LinkId(16), "High Water"   }, {14},
                    { RockySnow,    6,  4, LinkId(10), "Rocky Snow"   },
                    { Outpost,      8,  7, LinkId(13), "Outpost"      },
                    { HighOutpost, 12, 12, LinkId(17), "High Outpost" }, {18},

                    { 19,   0 }, {20}, {21},
                    { 22, 171 },
                    { 23,  45 },
                    { 24, 115 },
                    { 25,  87 },
                    { 26, 129 }, {27},
                    { 28,  59 },
                    { 29,  73 },
                    { 30, 143 }, {31},
                    { 32, 101 },
                    { 33, 157 },
                    { 34,  17 },
                    { 35,  31 }
                };

                static constexpr Span<uint16_t> terrainTypeMap{Jungle::terrainTypeMap};
            };

            struct Twilight
            {
                enum : size_t {
                    Water = 5,
                    Dirt = 2,
                    Mud = 4,
                    CrushedRock = 8,
                    Crevices = 15,
                    Flagstones = 11,
                    SunkenGround = 9,
                    Basilica = 16,
                    HighDirt = 3,
                    HighCrushedRock = 10,
                    HighFlagstones = 12,
                    HighSunkenGround = 13,
                    HighBasilica = 17,

                    Default = Dirt
                };

                static constexpr TerrainTypeInfo terrainTypeInfo[] { // TODO: Copy jungle & update names?
                    { 0,                14                                       }, {1},
                    { Dirt,              1,  1, LinkId( 1), "Dirt"               },
                    { HighDirt,          2,  8, LinkId( 2), "High Dirt"          },
                    { Mud,              13,  2, LinkId( 4), "Mud"                },
                    { Water,             3,  0, LinkId( 3), "Water"              }, {6}, {7},
                    { CrushedRock,       4,  3, LinkId( 8), "Crushed Rock"       },
                    { SunkenGround,      5,  6, LinkId(11), "Sunken Ground"      },
                    { HighCrushedRock,   9,  9, LinkId(14), "High Crushed Rock"  },
                    { Flagstones,        7,  5, LinkId(12), "Flagstones"         },
                    { HighFlagstones,   10, 10, LinkId(15), "High Flagstones"    },
                    { HighSunkenGround, 11, 11, LinkId(16), "High Sunken Ground" }, {14},
                    { Crevices,          6,  4, LinkId(10), "Crevices"           },
                    { Basilica,          8,  7, LinkId(13), "Basilica"           },
                    { HighBasilica,     12, 12, LinkId(17), "High Basilica"      }, {18},

                    { 19,   0 }, {20}, {21},
                    { 22, 171 },
                    { 23,  45 },
                    { 24, 115 },
                    { 25,  87 },
                    { 26, 129 }, {27},
                    { 28,  59 },
                    { 29,  73 },
                    { 30, 143 }, {31},
                    { 32, 101 },
                    { 33, 157 },
                    { 34,  17 },
                    { 35,  31 }
                };

                static constexpr Span<uint16_t> terrainTypeMap{Jungle::terrainTypeMap};
            };

            static constexpr size_t defaultBrushIndex[] {
                Badlands::Default, Space::Default, Installation::Default, Ashworld::Default,
                Jungle::Default, Desert::Default, Arctic::Default, Twilight::Default
            };
        };

        static constexpr Span<TerrainTypeInfo> tilesetTerrainTypes[] {
            Brush::Badlands::terrainTypeInfo, Brush::Space::terrainTypeInfo, Brush::Installation::terrainTypeInfo, Brush::Ashworld::terrainTypeInfo,
            Brush::Jungle::terrainTypeInfo, Brush::Desert::terrainTypeInfo, Brush::Arctic::terrainTypeInfo, Brush::Twilight::terrainTypeInfo
        };

        static constexpr Span<uint16_t> compressedTerrainTypeMaps[] {
            Brush::Badlands::terrainTypeMap, Brush::Space::terrainTypeMap, Brush::Installation::terrainTypeMap, Brush::Ashworld::terrainTypeMap,
            Brush::Jungle::terrainTypeMap, Brush::Desert::terrainTypeMap, Brush::Arctic::terrainTypeMap, Brush::Twilight::terrainTypeMap
        };

        static constexpr Span<size_t> defaultBrushIndex { Brush::defaultBrushIndex };
    };

    struct Terrain_ { // Terrain dat, altered from the Sc::Terrain found in Sc.h
        static constexpr size_t NumTilesets = 8;

#pragma pack(push, 1)
        __declspec(align(1)) struct Cv5Dat {
            static constexpr size_t MaxTileGroups = 1024;

            Sc::Isom::TileGroup tileGroups[MaxTileGroups];

            static inline size_t tileGroupsSize(size_t cv5FileSize) { return cv5FileSize/sizeof(Sc::Isom::TileGroup); }
        };
#pragma pack(pop)

        static constexpr uint16_t getTileGroup(uint16_t tileValue) { return tileValue / 16; }

        static constexpr uint16_t getSubtileValue(uint16_t tileValue) { return tileValue % 16; }

        struct Tiles
        {
            std::vector<Sc::Isom::TileGroup> tileGroups;

            std::vector<uint16_t> terrainTypeMap {};
            std::unordered_map<uint32_t, std::vector<uint16_t>> hashToTileGroup {};
            std::vector<Isom::ShapeLinks> isomLinks {};
            Span<Isom::TerrainTypeInfo> terrainTypes {};
            std::vector<Isom::TerrainTypeInfo> brushes {};
            Isom::TerrainTypeInfo defaultBrush {};
            
            inline void populateTerrainTypeMap(size_t tilesetIndex)
            {
                const auto & compressedTerrainTypeMap = Isom::compressedTerrainTypeMaps[tilesetIndex];

                size_t totalTerrainTypes = terrainTypes.size();
                terrainTypeMap.assign(totalTerrainTypes*totalTerrainTypes, uint16_t(0));
                std::vector<uint16_t> tempTypeMap(totalTerrainTypes*totalTerrainTypes, 0);
                std::vector<uint16_t> rowData {};

                // The compressedTerrainTypeMap maps terrain types to terrain types that isom searches start at, separated by zeroes
                for ( size_t i=0; compressedTerrainTypeMap[i] != 0; ++i )
                {
                    for ( size_t j=totalTerrainTypes*size_t(compressedTerrainTypeMap[i++]); compressedTerrainTypeMap[i] != 0; ++i,++j )
                        tempTypeMap[j] = compressedTerrainTypeMap[i];
                }

                // This expand the compressed type map to a square letting you use two types as x and y coordinates to get search start terrain types
                for ( int i=int(totalTerrainTypes)-1; i>=0; --i )
                {
                    rowData.assign(totalTerrainTypes, 0);
                    std::deque<uint16_t> terrainTypeStack { uint16_t(i) };
                    terrainTypeMap[totalTerrainTypes*i+terrainTypeStack[0]] = i;

                    while ( !terrainTypeStack.empty() )
                    {
                        uint16_t destRow = terrainTypeStack.front();
                        terrainTypeStack.pop_front();

                        size_t start = i*totalTerrainTypes;
                        for ( size_t j=destRow*totalTerrainTypes; tempTypeMap[j] != 0; ++j )
                        {
                            auto tempPath = tempTypeMap[j];
                            if ( terrainTypeMap[start+tempPath] == 0 )
                            {
                                uint16_t nextValue = rowData[destRow] == 0 ? tempPath : rowData[destRow];
                                terrainTypeStack.push_back(tempPath);
                                terrainTypeMap[start+tempPath] = nextValue;
                                rowData[tempPath] = nextValue;
                            }
                        }
                    }
                }
            }

            inline void generateIsomLinks()
            {
                size_t totalTileGroups = std::min(size_t(1024), tileGroups.size());
                Span<Sc::Isom::TileGroup> tilesetCv5s((Sc::Isom::TileGroup*)&tileGroups[0], totalTileGroups);

                std::vector<std::vector<uint16_t>> terrainTypeTileGroups(terrainTypes.size(), std::vector<uint16_t>{});
                for ( uint16_t i=0; i<totalTileGroups; i +=2 )
                {
                    if ( tilesetCv5s[i].terrainType > 0 )
                        terrainTypeTileGroups[tilesetCv5s[i].terrainType].push_back(i);
                }

                std::vector<Isom::TerrainTypeInfo> solidBrushes {};
                std::vector<Isom::TerrainTypeInfo> otherTerrainTypes {};
                size_t i = 1;
                for ( ; i<=terrainTypes.size()/2; ++i )
                {
                    if ( terrainTypes[i].isomValue != 0 )
                        solidBrushes.push_back(terrainTypes[i]);
                }
                for ( ; i<terrainTypes.size(); ++i )
                {
                    if ( terrainTypes[i].isomValue != 0 )
                        otherTerrainTypes.push_back({uint16_t(i), terrainTypes[i].isomValue});
                }
                std::sort(solidBrushes.begin(), solidBrushes.end(), [](const Isom::TerrainTypeInfo & l, const Isom::TerrainTypeInfo & r) {
                    return l.isomValue < r.isomValue;
                });
                std::sort(otherTerrainTypes.begin(), otherTerrainTypes.end(), [](const Isom::TerrainTypeInfo & l, const Isom::TerrainTypeInfo & r) {
                    return l.isomValue < r.isomValue;
                });

                for ( const auto & solidBrush : solidBrushes )
                {
                    while ( isomLinks.size() < size_t(solidBrush.isomValue) )
                        isomLinks.push_back(Isom::ShapeLinks{});

                    auto tileGroup = terrainTypeTileGroups[solidBrush.index][0];
                    const auto & links = tilesetCv5s[tileGroup].links;
                    isomLinks.push_back(
                        Isom::ShapeLinks{uint8_t(solidBrush.index),
                            {links.right, links.bottom, solidBrush.linkId},
                            {links.left, links.bottom, solidBrush.linkId},
                            {links.left, links.top, solidBrush.linkId},
                            {links.top, links.right, solidBrush.linkId}
                        }
                    );
                }

                size_t totalSolidBrushEntries = isomLinks.size();
                while ( isomLinks.size() < otherTerrainTypes[0].isomValue )
                    isomLinks.push_back(Isom::ShapeLinks{});

                for ( const auto & otherTerrainType : otherTerrainTypes )
                {
                    // In the isomLink table there are 14 shapes/entries per terrain types that are not solid brushes
                    size_t terrainTypeIsomLinkStart = isomLinks.size();
                    for ( size_t i=0; i<14; ++i )
                        isomLinks.push_back(Isom::ShapeLinks{uint8_t(otherTerrainType.index)});

                    const auto & tileGroupIndexes = terrainTypeTileGroups[otherTerrainType.index]; // All tile group indexes that belong to this terrain type
                    Isom::TerrainTypeShapes & shapes = (Isom::TerrainTypeShapes &)isomLinks[terrainTypeIsomLinkStart]; // Treat these 14 entries as a shapes struct
                    Isom::ShapeTileGroup shapeTileGroups[14] {}; // Record all tile group indexes that get used as shape quadrants

                    for ( auto tileGroupIndex : tileGroupIndexes )
                    {
                        const auto & tileGroup = tilesetCv5s[tileGroupIndex];

                        bool noStackAbove = (tileGroup.stackConnections.top == 0);
                        for ( size_t shapeIndex=0; shapeIndex<Isom::shapes.size(); ++shapeIndex )
                        {
                            if ( !tileGroup.links.isShapeQuadrant() )
                                continue; // Tile groups that have all hard links or no hard links do not refer to shape quadrants

                            // If this tile group matches any quadrants of this shape, update shape links & shapeTileGroups
                            const auto & checkShape = Isom::shapes[shapeIndex];
                            if ( checkShape.matches(Isom::Quadrant::TopLeft, tileGroup.links, noStackAbove) )
                            {
                                shapes[shapeIndex].topLeft.right = tileGroup.links.right;
                                shapes[shapeIndex].topLeft.bottom = tileGroup.links.bottom;
                                shapeTileGroups[shapeIndex].topLeft = tileGroupIndex;
                            }
                            if ( checkShape.matches(Isom::Quadrant::TopRight, tileGroup.links, noStackAbove) )
                            {
                                shapes[shapeIndex].topRight.left = tileGroup.links.left;
                                shapes[shapeIndex].topRight.bottom = tileGroup.links.bottom;
                                shapeTileGroups[shapeIndex].topRight = tileGroupIndex;
                            }
                            if ( checkShape.matches(Isom::Quadrant::BottomRight, tileGroup.links, noStackAbove) )
                            {
                                shapes[shapeIndex].bottomRight.left = tileGroup.links.left;
                                shapes[shapeIndex].bottomRight.top = tileGroup.links.top;
                                shapeTileGroups[shapeIndex].bottomRight = tileGroupIndex;
                            }
                            if ( checkShape.matches(Isom::Quadrant::BottomLeft, tileGroup.links, noStackAbove) )
                            {
                                shapes[shapeIndex].bottomLeft.top = tileGroup.links.top;
                                shapes[shapeIndex].bottomLeft.right = tileGroup.links.right;
                                shapeTileGroups[shapeIndex].bottomLeft = tileGroupIndex;
                            }
                        }
                    }

                    shapes.populateJutInEastWest(tilesetCv5s, shapeTileGroups);
                    shapes.populateEmptyQuadrantLinks();
                    shapes.populateHardcodedLinkIds();
                    shapes.populateLinkIdsToSolidBrushes(tilesetCv5s, shapeTileGroups, totalSolidBrushEntries, isomLinks);
                }
            }

            inline void loadIsom(size_t tilesetIndex)
            {
                Span<Isom::TerrainTypeInfo> terrainTypeInfo = Isom::tilesetTerrainTypes[tilesetIndex];
                this->terrainTypes = terrainTypeInfo;
                populateTerrainTypeMap(tilesetIndex);

                for ( size_t i=0; i<tileGroups.size(); i+=2 )
                {
                    const auto & groupLinks = tileGroups[i].links;
                    uint32_t left = uint32_t(groupLinks.left);
                    uint32_t top = uint32_t(groupLinks.top);
                    uint32_t right = uint32_t(groupLinks.right);
                    uint32_t bottom = uint32_t(groupLinks.bottom);

                    uint32_t tileGroupHash = (((left << 6 | top) << 6 | right) << 6 | bottom) << 6;
                    if ( left >= 48 || top >= 48 || right >= 48 || bottom >= 48 )
                        tileGroupHash |= tileGroups[i].terrainType;

                    auto existing = hashToTileGroup.find(tileGroupHash);
                    if ( existing != hashToTileGroup.end() )
                        existing->second.push_back(uint16_t(i));
                    else
                        hashToTileGroup.insert(std::make_pair(tileGroupHash, std::vector<uint16_t>{uint16_t(i)}));
                }

                generateIsomLinks();

                for ( const auto & terrainType : terrainTypeInfo )
                {
                    if ( terrainType.brushSortOrder >= 0 )
                        brushes.push_back(terrainType);
                }
                std::sort(brushes.begin(), brushes.end(), [&](const Isom::TerrainTypeInfo & l, const Isom::TerrainTypeInfo & r) {
                    return l.brushSortOrder < r.brushSortOrder;
                });
                defaultBrush = terrainTypeInfo[Isom::defaultBrushIndex[tilesetIndex]];
            }

            inline bool load(size_t tilesetIndex, const std::vector<ArchiveFilePtr> & orderedSourceFiles, const std::string & tilesetName)
            {
                const std::string tilesetMpqDirectory = "tileset";
                const std::string mpqFilePath = makeMpqFilePath(tilesetMpqDirectory, tilesetName);
                const std::string cv5FilePath = makeExtMpqFilePath(mpqFilePath, "cv5");
    
                auto cv5Data = Sc::Data::GetAsset(orderedSourceFiles, cv5FilePath);

                if ( cv5Data )
                {
                    if ( cv5Data->size() % sizeof(Sc::Isom::TileGroup) == 0 )
                    {
                        size_t numTileGroups = Cv5Dat::tileGroupsSize(cv5Data->size());

                        if ( numTileGroups > 0 )
                        {
                            Sc::Isom::TileGroup* rawTileGroups = (Sc::Isom::TileGroup*)&cv5Data.value()[0];
                            tileGroups.assign(&rawTileGroups[0], &rawTileGroups[numTileGroups]);
                        }
                        else
                            tileGroups.clear();

                        loadIsom(tilesetIndex);

                        return true;
                    }
                    else
                        logger.error() << "One or more files improperly sized for tileset " << mpqFilePath << std::endl;
                }
                else
                    logger.error() << "Failed to get one or more files for tileset " << mpqFilePath << std::endl;

                return false;
            }

            static inline size_t getGroupIndex(const u16 & tileIndex) { return size_t(tileIndex / 16); }

            static inline size_t getGroupMemberIndex(const u16 & tileIndex) { return size_t(tileIndex & 0xF); }
        };

        inline const Tiles & get(const Sc::Terrain::Tileset & tileset) const
        {
            if ( tileset < NumTilesets )
                return tilesets[tileset];
            else
                return tilesets[tileset % NumTilesets];
        }

        inline bool load(const std::vector<ArchiveFilePtr> & orderedSourceFiles)
        {
            auto start = std::chrono::high_resolution_clock::now();
            bool success = true;
            for ( size_t i=0; i<NumTilesets; i++ )
                success &= tilesets[i].load(i, orderedSourceFiles, Sc::Terrain::TilesetNames[i]);
    
            auto finish = std::chrono::high_resolution_clock::now();
            logger.debug() << "Terrain loading completed in " << std::chrono::duration_cast<std::chrono::milliseconds>(finish-start).count() << "ms" << std::endl;
            return success;
        }

        inline bool load(const std::string & expectedStarCraftDirectory)
        {
            auto start = std::chrono::high_resolution_clock::now();
            logger.debug("Loading StarCraft Data...");

            Sc::DataFile::BrowserPtr dataFileBrowser = std::make_shared<Sc::DataFile::Browser>();
            const std::vector<Sc::DataFile::Descriptor> & dataFiles = Sc::DataFile::getDefaultDataFiles();
            FileBrowserPtr<u32> starCraftBrowser = Sc::DataFile::Browser::getDefaultStarCraftBrowser();

            const std::vector<ArchiveFilePtr> orderedSourceFiles = dataFileBrowser->openScDataFiles(dataFiles, expectedStarCraftDirectory, starCraftBrowser);
            if ( orderedSourceFiles.empty() )
            {
                logger.error("No archives selected, many features will not work without the game files.\n\nInstall or locate StarCraft for the best experience.");
                return false;
            }

            if ( !load(orderedSourceFiles) )
                CHKD_ERR("Failed to load terrain dat");
    
            auto finish = std::chrono::high_resolution_clock::now();
            logger.debug() << "StarCraft data loading completed in " << std::chrono::duration_cast<std::chrono::milliseconds>(finish-start).count() << "ms" << std::endl;
            return true;
        }

    private:
        Tiles tilesets[NumTilesets];
    };
}

namespace Chk {

    struct IsomRect
    {
        struct EditorFlag_ {
            enum uint16_t_ : uint16_t {
                Modified = 0x0001,
                Visited = 0x8000,

                xModified = 0xFFFE,
                xVisited = 0x7FFF,

                ClearAll = 0x7FFE
            };
        };
        using EditorFlag = EditorFlag_::uint16_t_;

        struct Point
        {
            size_t x;
            size_t y;
        };

        struct IsomDiamond // A "diamond" exists along the isometric coordinate space and has a projection to an 8x4 rectangular shape with four quadrants
        {
            struct Neighbor_ {
                enum int_ : int {
                    UpperLeft,
                    UpperRight,
                    LowerRight,
                    LowerLeft
                };
            };
            using Neighbor = Neighbor_::int_;
            static constexpr Neighbor neighbors[] { Neighbor::UpperLeft, Neighbor::UpperRight, Neighbor::LowerRight, Neighbor::LowerLeft };

            size_t x;
            size_t y;

            constexpr IsomDiamond getNeighbor(Neighbor neighbor) const {
                switch ( neighbor ) {
                    case Neighbor::UpperLeft: return { x - 1, y - 1 };
                    case Neighbor::UpperRight: return { x + 1, y - 1 };
                    case Neighbor::LowerRight: return { x + 1, y + 1 };
                    default: /*Neighbor::LowerLeft*/ return { x - 1, y + 1 };
                }
            }
            constexpr Point getRectangleCoords(Sc::Isom::Quadrant quadrant) const {
                switch ( quadrant ) {
                    case Sc::Isom::Quadrant::TopLeft: return Point { x - 1, y - 1 };
                    case Sc::Isom::Quadrant::TopRight: return Point { x, y - 1 };
                    case Sc::Isom::Quadrant::BottomRight: return Point { x, y }; // Diamond (x, y) is the same as the diamonds bottom-right rectangle (x, y)
                    default: /*Sc::Isom::Quadrant::BottomLeft*/ return Point { x - 1, y };
                }
            }
            constexpr operator Point() const { return { x, y }; } // Conversion implies going to the bottom-right rectangle for the isom diamond
            constexpr bool isValid() const { return (x+y)%2 == 0; }
        };

        uint16_t left = 0;
        uint16_t top = 0;
        uint16_t right = 0;
        uint16_t bottom = 0;

        constexpr IsomRect() = default;
        constexpr IsomRect(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom) : left(left), top(top), right(right), bottom(bottom) {}

        constexpr uint16_t & side(Sc::Isom::Side side) {
            switch ( side ) {
                case Sc::Isom::Side::Left: return left;
                case Sc::Isom::Side::Top: return top;
                case Sc::Isom::Side::Right: return right;
                default: /*Sc::Isom::Side::Bottom*/ return bottom;
            }
        }

        constexpr uint16_t getIsomValue(Sc::Isom::Side side) const {
            switch ( side ) {
                case Sc::Isom::Side::Left: return left & EditorFlag::ClearAll;
                case Sc::Isom::Side::Top: return top & EditorFlag::ClearAll;
                case Sc::Isom::Side::Right: return right & EditorFlag::ClearAll;
                default: /*Sc::Isom::Side::Bottom*/ return bottom & EditorFlag::ClearAll;
            }
        }

        constexpr void setIsomValue(Sc::Isom::Side side, uint16_t value) {
            this->side(side) = value;
        }

        constexpr uint32_t getHash(Span<Sc::Isom::ShapeLinks> isomLinks) const
        {
            uint32_t hash = 0;
            uint16_t lastTerrainType = 0;
            for ( auto side : Sc::Isom::sides )
            {
                auto isomValue = this->getIsomValue(side);
                const auto & shapeLinks = isomLinks[isomValue >> 4];
                auto edgeLink = shapeLinks.getEdgeLink(isomValue);
                hash = (hash | uint32_t(edgeLink)) << 6;

                if ( shapeLinks.terrainType != 0 && edgeLink > Sc::Isom::Link::SoftLinks )
                    lastTerrainType = shapeLinks.terrainType;
            }
            return hash | lastTerrainType; // 6 bits per component (left, top, right, bottom, terrainType)
        }

        constexpr void set(Sc::Isom::ProjectedQuadrant quadrant, uint16_t value) {
            setIsomValue(quadrant.firstSide, (value << 4) | quadrant.firstEdgeFlags);
            setIsomValue(quadrant.secondSide, (value << 4) | quadrant.secondEdgeFlags);
        }

        constexpr bool isLeftModified() const { return left & EditorFlag::Modified; }

        constexpr bool isLeftOrRightModified() const { return ((left | right) & EditorFlag::Modified) == EditorFlag::Modified; }

        constexpr void setModified(Sc::Isom::ProjectedQuadrant quadrant) {
            this->side(quadrant.firstSide) |= EditorFlag::Modified;
            this->side(quadrant.secondSide) |= EditorFlag::Modified;
        }

        constexpr bool isVisited() const { return (right & EditorFlag::Visited) == EditorFlag::Visited; }

        constexpr void setVisited() { right |= EditorFlag::Visited; }

        constexpr void clearEditorFlags() {
            left &= EditorFlag::ClearAll;
            top &= EditorFlag::ClearAll;
            right &= EditorFlag::ClearAll;
            bottom &= EditorFlag::ClearAll;
        }

        constexpr void clear() {
            left = 0;
            top = 0;
            right = 0;
            bottom = 0;
        }

        REFLECT(IsomRect, left, top, right, bottom)
    };
    static_assert(sizeof(IsomRect) == 8, "IsomRect must be exactly 8 bytes!");

    using IsomDiamond = IsomRect::IsomDiamond;

    #pragma pack(push, 1)
    struct IsomRectUndo {
        Chk::IsomDiamond diamond {};
        Chk::IsomRect oldValue {};
        Chk::IsomRect newValue {};

        constexpr void setOldValue(const Chk::IsomRect & oldValue) {
            this->oldValue.left = oldValue.left & Chk::IsomRect::EditorFlag::ClearAll;
            this->oldValue.right = oldValue.right & Chk::IsomRect::EditorFlag::ClearAll;
            this->oldValue.top = oldValue.top & Chk::IsomRect::EditorFlag::ClearAll;
            this->oldValue.bottom = oldValue.bottom & Chk::IsomRect::EditorFlag::ClearAll;
        }

        constexpr void setNewValue(const Chk::IsomRect & newValue) {
            this->newValue.left = newValue.left & Chk::IsomRect::EditorFlag::ClearAll;
            this->newValue.right = newValue.right & Chk::IsomRect::EditorFlag::ClearAll;
            this->newValue.top = newValue.top & Chk::IsomRect::EditorFlag::ClearAll;
            this->newValue.bottom = newValue.bottom & Chk::IsomRect::EditorFlag::ClearAll;
        }

        IsomRectUndo(Chk::IsomDiamond diamond, const Chk::IsomRect & oldValue, const Chk::IsomRect & newValue)
            : diamond(diamond)
        {
            setOldValue(oldValue);
            setNewValue(newValue);
        }
    };

    // IsomCache holds all the data required to edit isometric terrain which is not a part of scenario; as well as methods that operate on said data exclusively
    // IsomCache is invalidated & must be re-created whenever tileset, map width, or map height changes
    struct IsomCache
    {
        Sc::Terrain::Tileset tileset; // If tileset changes the cache should be recreated with the new tileset
        size_t isomWidth; // This is a sort of isometric width, not tileWidth
        size_t isomHeight; // This is a sort of isometric height, not tileHeight
        Sc::BoundingBox changedArea {};

        std::vector<std::optional<IsomRectUndo>> undoMap {}; // Undo per x, y coordinate

        Span<Sc::Isom::TileGroup> tileGroups {};
        Span<Sc::Isom::ShapeLinks> isomLinks {};
        Span<Sc::Isom::TerrainTypeInfo> terrainTypes {};
        Span<uint16_t> terrainTypeMap {};
        const std::unordered_map<uint32_t, std::vector<uint16_t>>* hashToTileGroup;

        inline IsomCache(Sc::Terrain::Tileset tileset, size_t tileWidth, size_t tileHeight, const Sc::Terrain_::Tiles & tilesetData) :
            tileset(tileset),
            isomWidth(tileWidth/2 + 1),
            isomHeight(tileHeight + 1),
            tileGroups(&tilesetData.tileGroups[0], tilesetData.tileGroups.size()),
            isomLinks(&tilesetData.isomLinks[0], tilesetData.isomLinks.size()),
            terrainTypes(&tilesetData.terrainTypes[0], tilesetData.terrainTypes.size()),
            terrainTypeMap(&tilesetData.terrainTypeMap[0], tilesetData.terrainTypeMap.size()),
            hashToTileGroup(&tilesetData.hashToTileGroup),
            undoMap(isomWidth*isomHeight, std::nullopt)
        {
            resetChangedArea();
        }

        constexpr void resetChangedArea()
        {
            changedArea.left = isomWidth;
            changedArea.right = 0;
            changedArea.top = isomHeight;
            changedArea.bottom = 0;
        }

        constexpr void setAllChanged()
        {
            changedArea.left = 0;
            changedArea.right = isomWidth-1;
            changedArea.top = 0;
            changedArea.bottom = isomHeight-1;
        }

        constexpr uint16_t getTerrainTypeIsomValue(size_t terrainType) const
        {
            return terrainType < terrainTypes.size() ? terrainTypes[terrainType].isomValue : 0;
        }

        inline uint16_t getRandomSubtile(uint16_t tileGroup) const
        {
            if ( tileGroup < tileGroups.size() )
            {
                size_t totalCommon = 0;
                size_t totalRare = 0;
                for ( ; totalCommon < 16 && tileGroups[tileGroup].megaTileIndex[totalCommon] != 0; ++totalCommon );
                for ( ; totalCommon+totalRare+1 < 16 && tileGroups[tileGroup].megaTileIndex[totalCommon+totalRare+1] != 0; ++totalRare );

                if ( totalRare != 0 && std::rand() <= RAND_MAX / 20 ) // 1 in 20 chance of using a rare tile
                    return 16*tileGroup + uint16_t(totalCommon + 1 + (std::rand() % totalRare)); // Select particular rare tile
                else if ( totalCommon != 0 ) // Use a common tile
                    return 16*tileGroup + uint16_t(std::rand() % totalCommon); // Select particular common tile
            }
            return 16*tileGroup; // Default/fall-back to first tile in group
        }

        virtual inline void addIsomUndo(const IsomRectUndo & /*isomUndo*/) {} // Does nothing unless overridden

        // Call when one undoable operation is complete, e.g. resize a map, or mouse up after pasting/brushing some terrain
        // When changing lots of terrain (e.g. by holding the mouse button and moving around), undos are blocked from being added to the same tiles multiple times
        // Calling this method clears out said blockers
        inline void finalizeUndoableOperation()
        {
            undoMap.assign(isomWidth*isomHeight, std::nullopt); // Clears out the undoMap so new entries can be set
        }
    };
    #pragma pack(pop)
}

struct ScMap
{
    uint16_t tileWidth;
    uint16_t tileHeight;
    Sc::Terrain::Tileset tileset {Sc::Terrain::Tileset::Badlands};
    std::vector<u16> tiles {};
    std::vector<u16> editorTiles {};
    std::vector<Chk::IsomRect> isomRects {};
    
    constexpr size_t getIsomWidth() const { return size_t(tileWidth)/2 + 1; }
    constexpr size_t getIsomHeight() const { return size_t(tileHeight) + 1; }
    Chk::IsomRect & getIsomRect(size_t isomRectIndex)
    {
        if ( isomRectIndex < this->isomRects.size() )
            return this->isomRects[isomRectIndex];
        else
            throw std::out_of_range(std::string("IsomRectIndex: ") + std::to_string(isomRectIndex) + " is past the end of the ISOM section!");
    }
    const Chk::IsomRect & getIsomRect(size_t isomRectIndex) const
    {
        if ( isomRectIndex < this->isomRects.size() )
            return this->isomRects[isomRectIndex];
        else
            throw std::out_of_range(std::string("IsomRectIndex: ") + std::to_string(isomRectIndex) + " is past the end of the ISOM section!");
    }
    
    bool placeIsomTerrain(Chk::IsomDiamond isomDiamond, size_t terrainType, size_t brushExtent, Chk::IsomCache & cache)
    {
        uint16_t isomValue = cache.getTerrainTypeIsomValue(terrainType);
        if ( isomValue == 0 || !isomDiamond.isValid() || size_t(isomValue) >= cache.isomLinks.size() || cache.isomLinks[size_t(isomValue)].terrainType == 0 )
            return false;

        int brushMin = int(brushExtent) / -2;
        int brushMax = brushMin + int(brushExtent);
        if ( brushExtent%2 == 0 ) {
            ++brushMin;
            ++brushMax;
        }

        cache.resetChangedArea();

        std::deque<Chk::IsomDiamond> diamondsToUpdate {};
        for ( int brushOffsetX=brushMin; brushOffsetX<brushMax; ++brushOffsetX )
        {
            for ( int brushOffsetY=brushMin; brushOffsetY<brushMax; ++brushOffsetY )
            {
                size_t brushX = isomDiamond.x + brushOffsetX - brushOffsetY;
                size_t brushY = isomDiamond.y + brushOffsetX + brushOffsetY;
                if ( isInBounds({brushX, brushY}) )
                {
                    setDiamondIsomValues({brushX, brushY}, isomValue, true, cache);
                    if ( brushOffsetX == brushMin || brushOffsetX == brushMax-1 || brushOffsetY == brushMin || brushOffsetY == brushMax-1 )
                    { // Mark diamonds on the edge of the brush for radial updates
                        for ( auto i : Chk::IsomDiamond::neighbors )
                        {
                            Chk::IsomDiamond neighbor = Chk::IsomDiamond{brushX, brushY}.getNeighbor(i);
                            if ( diamondNeedsUpdate(neighbor) )
                                diamondsToUpdate.push_back(Chk::IsomDiamond{neighbor.x, neighbor.y});
                        }
                    }
                }
            }
        }
        radiallyUpdateTerrain(true, diamondsToUpdate, cache);
        return true;
    }
    void copyIsomFrom(const ScMap & sourceMap, int32_t xTileOffset, int32_t yTileOffset, bool undoable, Chk::IsomCache & destCache)
    {
        size_t sourceIsomWidth = sourceMap.tileWidth/2 + 1;
        size_t sourceIsomHeight = sourceMap.tileHeight + 1;

        if ( undoable )
        {
            for ( size_t y=0; y<destCache.isomHeight; ++y )
            {
                for ( size_t x=0; x<destCache.isomWidth; ++x )
                    addIsomUndo({x, y}, destCache);
            }
        }

        int32_t diamondX = xTileOffset / 2;
        int32_t diamondY = yTileOffset;

        Sc::BoundingBox sourceRc { sourceIsomWidth, sourceIsomHeight, destCache.isomWidth, destCache.isomHeight, diamondX, diamondY };

        for ( size_t y=sourceRc.top; y<sourceRc.bottom; ++y )
        {
            const Chk::IsomRect* sourceRow = &sourceMap.isomRects[y*sourceIsomWidth + sourceRc.left];
            Chk::IsomRect* destRow = &isomRects[(y+diamondY)*destCache.isomWidth + sourceRc.left + diamondX];
            std::memcpy(destRow, sourceRow, sizeof(Chk::IsomRect)*(sourceRc.right-sourceRc.left));
        }

        if ( undoable )
        {
            // Clear out-of-bounds isom values
            for ( size_t y=sourceIsomHeight; y<destCache.isomHeight; ++y )
            {
                for ( size_t x=0; x<destCache.isomWidth; ++x )
                    isomRectAt({x, y}).clear();
            }

            if ( sourceIsomWidth < destCache.isomWidth )
            {
                for ( size_t y=0; y<destCache.isomHeight; ++y )
                {
                    for ( size_t x=sourceIsomWidth; x<destCache.isomWidth; ++x )
                        isomRectAt({x, y}).clear();
                }
            }

            for ( size_t y=0; y<destCache.isomHeight; ++y )
            {
                for ( size_t x=0; x<destCache.isomWidth; ++x )
                    destCache.undoMap[y*destCache.isomWidth + x]->setNewValue(getIsomRect({x, y})); // Update undo info for this position
            }
        }
    }
    void updateTilesFromIsom(Chk::IsomCache & cache)
    {
        for ( size_t y=cache.changedArea.top; y<=cache.changedArea.bottom; ++y )
        {
            for ( size_t x=cache.changedArea.left; x<=cache.changedArea.right; ++x )
            {
                Chk::IsomRect & isomRect = isomRectAt({x, y});
                if ( isomRect.isLeftOrRightModified() )
                    updateTileFromIsom({x, y}, cache);

                isomRect.clearEditorFlags();
            }
        }
        cache.resetChangedArea();
    }
    bool resizeIsom(int32_t xTileOffset, int32_t yTileOffset, size_t oldMapWidth, size_t oldMapHeight, bool fixBorders, Chk::IsomCache & cache)
    {
        int32_t xDiamondOffset = xTileOffset/2;
        int32_t yDiamondOffset = yTileOffset;
        size_t oldIsomWidth = oldMapWidth/2 + 1;
        size_t oldIsomHeight = oldMapHeight + 1;
        Sc::BoundingBox sourceRc { oldIsomWidth, oldIsomHeight, cache.isomWidth, cache.isomHeight, xDiamondOffset, yDiamondOffset };
        Sc::BoundingBox innerArea {
            sourceRc.left+xDiamondOffset, sourceRc.top+yDiamondOffset, sourceRc.right+xDiamondOffset-1, sourceRc.bottom+yDiamondOffset-1
        };

        std::vector<Chk::IsomDiamond> edges {};
        for ( size_t y=innerArea.top; y<=innerArea.bottom; ++y )
        {
            for ( size_t x=innerArea.left+(innerArea.left+y)%2; x<innerArea.right+1; x+= 2 )
            {
                if ( (x+y)%2 != 0 )
                    continue;

                bool fullyInside = true;
                bool fullyOutside = true;
                uint16_t isomValue = 0;
                for ( auto i : Sc::Isom::quadrants )
                {
                    Chk::IsomRect::Point rectCoords = Chk::IsomDiamond{x, y}.getRectangleCoords(i);
                    if ( isInBounds(rectCoords) )
                    {
                        if ( rectCoords.x >= innerArea.left && rectCoords.x < innerArea.right &&
                            rectCoords.y >= innerArea.top && rectCoords.y < innerArea.bottom )
                        {
                            isomValue = getIsomRect(rectCoords).getIsomValue(Sc::Isom::ProjectedQuadrant{i}.firstSide) >> 4;
                            fullyOutside = false;
                        }
                        else
                            fullyInside = false;
                    }
                }

                if ( fullyOutside ) // Do not update diamonds completely outside the inner area
                    continue;

                if ( !fullyInside ) // Update diamonds that are partially inside and mark them for radial updates
                {
                    for ( auto i : Sc::Isom::quadrants )
                    {
                        Chk::IsomRect::Point rectCoords = Chk::IsomDiamond{x, y}.getRectangleCoords(i);
                        if ( (rectCoords.x < innerArea.left || rectCoords.x >= innerArea.right || // Quadrant is outside inner area
                            rectCoords.y < innerArea.top || rectCoords.y >= innerArea.bottom) )
                        {
                            setIsomValue(rectCoords, Sc::Isom::quadrants[size_t(i)], isomValue, false, cache);
                        }
                    }

                    if ( fixBorders )
                    {
                        for ( auto i : Chk::IsomDiamond::neighbors )
                        {
                            Chk::IsomDiamond neighbor = Chk::IsomDiamond{x, y}.getNeighbor(i);
                            if ( isInBounds(neighbor) && (
                                neighbor.x < innerArea.left || neighbor.x > innerArea.right || // Neighbor is outside inner area
                                neighbor.y < innerArea.top || neighbor.y > innerArea.bottom) )
                            {
                                edges.push_back(neighbor);
                            }
                        }
                    }
                }

                for ( auto i : Sc::Isom::quadrants )
                {
                    Chk::IsomRect::Point rectCoords = Chk::IsomDiamond{x, y}.getRectangleCoords(i);
                    if ( isInBounds(rectCoords) )
                        isomRectAt(rectCoords).setModified(i);
                }
            }
        }

        // Order edges by distance from top-left over difference between x&y over x-coordinates
        std::sort(edges.begin(), edges.end(), [](const Chk::IsomDiamond & l, const Chk::IsomDiamond & r) {
            auto lDistance = l.x + l.y;
            auto rDistance = r.x + r.y;
            if ( lDistance != rDistance )
                return lDistance < rDistance; // Order by distance from top-left corner

            lDistance = std::max(l.x, l.y) - std::min(l.x, l.y);
            rDistance = std::max(r.x, r.y) - std::min(r.x, r.y);
            if ( lDistance != rDistance )
                return lDistance < rDistance; // Order by difference between x & y
            else
                return l.x < r.x; // Order by x difference
        });

        // Update all the edges
        std::deque<Chk::IsomDiamond> diamondsToUpdate;
        for ( const auto & edge : edges )
        {
            if ( diamondNeedsUpdate({edge.x, edge.y}) )
                diamondsToUpdate.push_back({edge.x, edge.y});
        }
        radiallyUpdateTerrain(false, diamondsToUpdate, cache);

        // Clear the changed and visited flags
        for ( size_t y=cache.changedArea.top; y<=cache.changedArea.bottom; ++y )
        {
            for ( size_t x=cache.changedArea.left; x<=cache.changedArea.right; ++x )
                isomRectAt({x, y}).clearEditorFlags();
        }

        for ( size_t y=innerArea.top; y<=innerArea.bottom; ++y )
        {
            for ( size_t x=innerArea.left+(innerArea.left+y)%2; x<=innerArea.right; x+=2 )
            {
                if ( (x+y)%2 != 0 )
                    continue;

                bool fullyOutside = true;
                for ( auto i : Sc::Isom::quadrants )
                {
                    Chk::IsomRect::Point rectCoords = Chk::IsomDiamond{x, y}.getRectangleCoords(i);
                    if ( isInBounds(rectCoords) &&
                        rectCoords.x >= innerArea.left && rectCoords.x < innerArea.right && // Inside inner area
                        rectCoords.y >= innerArea.top && rectCoords.y < innerArea.bottom )
                    {
                        fullyOutside = false;
                        break;
                    }
                }

                if ( !fullyOutside ) // Only update diamonds that are at least partially inside
                {
                    for ( auto i : Sc::Isom::quadrants )
                    {
                        Chk::IsomRect::Point rectCoords = Chk::IsomDiamond{x, y}.getRectangleCoords(i);
                        if ( isInBounds(rectCoords) )
                            isomRectAt(rectCoords).setModified(i);
                    }
                }
            }
        }
        diamondsToUpdate.clear();

        cache.setAllChanged();

        // Clear off the changed flags for the inner area
        for ( size_t y=innerArea.top; y<innerArea.bottom; ++y )
        {
            for ( size_t x=innerArea.left; x<innerArea.right; ++x )
                isomRectAt({x, y}).clearEditorFlags();
        }

        for ( size_t y=0; y<cache.isomHeight; ++y )
        {
            for ( size_t x=y%2; x<cache.isomWidth; x+=2 )
            {
                if ( (x+y)%2 != 0 )
                    continue;

                bool fullyInside = true;
                for ( auto i : Sc::Isom::quadrants )
                {
                    Chk::IsomRect::Point rectCoords = Chk::IsomDiamond{x, y}.getRectangleCoords(i);
                    if ( isInBounds(rectCoords) &&
                        (rectCoords.x < innerArea.left || rectCoords.x >= innerArea.right || // Quadrant is outside the inner area
                            rectCoords.y < innerArea.top || rectCoords.y < innerArea.bottom) )
                    {
                        fullyInside = false;
                        break;
                    }
                }

                if ( !fullyInside ) // Mark diamonds partially or fully outside the inner area as modified
                {
                    for ( auto i : Sc::Isom::quadrants )
                    {
                        Chk::IsomRect::Point rectCoords = Chk::IsomDiamond{x, y}.getRectangleCoords(i);
                        if ( isInBounds(rectCoords) )
                            isomRectAt(rectCoords).setModified(i);
                    }
                }
            }
        }

        return true;
    }

private:
    inline uint16_t getTileValue(size_t tileX, size_t tileY) const { return editorTiles[tileY*tileWidth + tileX]; }
    inline void setTileValue(size_t tileX, size_t tileY, uint16_t tileValue)
    {
        editorTiles[tileY*tileWidth + tileX] = tileValue;
        // TODO: should check if doodads need to be deleted, then copy invalidated TILE area overlayed with doodads to form MTXM
        tiles[tileY*tileWidth + tileX] = tileValue;
    }
    inline uint16_t getCentralIsomValue(Chk::IsomRect::Point point) const { return isomRects[point.y*getIsomWidth() + point.x].left >> 4; }
    inline bool centralIsomValueModified(Chk::IsomRect::Point point) const { return isomRects[point.y*getIsomWidth() + point.x].isLeftModified(); }
    inline const Chk::IsomRect & getIsomRect(Chk::IsomRect::Point point) const { return isomRects[point.y*getIsomWidth() + point.x]; }
    inline Chk::IsomRect & isomRectAt(Chk::IsomRect::Point point) { return isomRects[point.y*getIsomWidth() + point.x]; }
    constexpr bool isInBounds(Chk::IsomRect::Point point) const { return point.x < getIsomWidth() && point.y < getIsomHeight(); }

    inline void addIsomUndo(Chk::IsomRect::Point point, Chk::IsomCache & cache)
    {
        if ( !cache.undoMap[point.y*cache.isomWidth + point.x] ) // if undoMap entry doesn't already exist at this position...
        {
            Chk::IsomRectUndo isomRectUndo(Chk::IsomDiamond{point.x, point.y}, getIsomRect(point), Chk::IsomRect{});
            cache.undoMap[point.y*cache.isomWidth + point.x] = isomRectUndo; // add undoMap entry at position
            cache.addIsomUndo(isomRectUndo);
        }
    }
    inline bool diamondNeedsUpdate(Chk::IsomDiamond isomDiamond) const
    {
        return isInBounds(isomDiamond) &&
            !centralIsomValueModified(isomDiamond) &&
            getCentralIsomValue(isomDiamond) != 0;
    }
    inline void setIsomValue(Chk::IsomRect::Point isomDiamond, Sc::Isom::Quadrant shapeQuadrant, uint16_t isomValue, bool undoable, Chk::IsomCache & cache)
    {
        if ( isInBounds(isomDiamond) )
        {
            Chk::IsomRectUndo* isomUndo = nullptr;
            size_t isomRectIndex = isomDiamond.y*cache.isomWidth + size_t(isomDiamond.x);
            if ( undoable && isomRectIndex < cache.undoMap.size() )
            {
                addIsomUndo(isomDiamond, cache);
                isomUndo = cache.undoMap[isomRectIndex] ? &cache.undoMap[isomRectIndex].value() : nullptr;
            }

            Chk::IsomRect & rect = isomRectAt(isomDiamond);
            rect.set(shapeQuadrant, isomValue);
            rect.setModified(shapeQuadrant);
            cache.changedArea.expandToInclude(isomDiamond.x, isomDiamond.y);

            if ( isomUndo != nullptr ) // Update the undo if it was present prior to the changes
                isomUndo->setNewValue(rect);
        }
    }
    inline void setDiamondIsomValues(Chk::IsomDiamond isomDiamond, uint16_t isomValue, bool undoable, Chk::IsomCache & cache)
    {
        setIsomValue(isomDiamond.getRectangleCoords(Sc::Isom::Quadrant::TopLeft), Sc::Isom::Quadrant::TopLeft, isomValue, undoable, cache);
        setIsomValue(isomDiamond.getRectangleCoords(Sc::Isom::Quadrant::TopRight), Sc::Isom::Quadrant::TopRight, isomValue, undoable, cache);
        setIsomValue(isomDiamond.getRectangleCoords(Sc::Isom::Quadrant::BottomRight), Sc::Isom::Quadrant::BottomRight, isomValue, undoable, cache);
        setIsomValue(isomDiamond.getRectangleCoords(Sc::Isom::Quadrant::BottomLeft), Sc::Isom::Quadrant::BottomLeft, isomValue, undoable, cache);
    }

    struct IsomNeighbors
    {
        struct BestMatch
        {
            uint16_t isomValue = 0;
            uint16_t matchCount = 0;
        };

        struct NeighborQuadrant
        {
            Sc::Isom::LinkId linkId = Sc::Isom::LinkId::None;
            uint16_t isomValue = 0;
            bool modified = false;
        };
        
        NeighborQuadrant upperLeft {};
        NeighborQuadrant upperRight {};
        NeighborQuadrant lowerRight {};
        NeighborQuadrant lowerLeft {};

        uint8_t maxModifiedOfFour = 0;
        BestMatch bestMatch {};

        constexpr NeighborQuadrant & operator[](Sc::Isom::Quadrant i) {
            switch ( i ) {
                case Sc::Isom::Quadrant::TopLeft: return upperLeft;
                case Sc::Isom::Quadrant::TopRight: return upperRight;
                case Sc::Isom::Quadrant::BottomRight: return lowerRight;
                default: /*Quadrant::BottomLeft*/ return lowerLeft;
            }
        }

        constexpr NeighborQuadrant & operator[](size_t i) { return (*this)[Sc::Isom::Quadrant(i)]; }
    };
    void loadNeighborInfo(Chk::IsomDiamond isomDiamond, IsomNeighbors & neighbors, Span<Sc::Isom::ShapeLinks> isomLinks) const
    {
        for ( auto i : Chk::IsomDiamond::neighbors ) // Gather info about the four neighboring isom diamonds/isom shapes
        {
            Chk::IsomDiamond neighbor = isomDiamond.getNeighbor(i);
            if ( isInBounds(neighbor) )
            {
                uint16_t isomValue = getCentralIsomValue(neighbor);
                neighbors[i].modified = centralIsomValueModified(neighbor);
                neighbors[i].isomValue = isomValue;
                if ( isomValue < isomLinks.size() )
                {
                    neighbors[i].linkId = isomLinks[isomValue].getLinkId(Sc::Isom::OppositeQuadrant(i));
                    if ( neighbors[i].modified && isomLinks[isomValue].terrainType > neighbors.maxModifiedOfFour )
                        neighbors.maxModifiedOfFour = isomLinks[isomValue].terrainType;
                }
            }
        }
    }
    uint16_t countNeighborMatches(const Sc::Isom::ShapeLinks & shapeLinks, IsomNeighbors & neighbors, Span<Sc::Isom::ShapeLinks> isomLinks) const
    {
        auto terrainType = shapeLinks.terrainType;
        uint16_t totalMatches = 0;
        for ( auto quadrant : Sc::Isom::quadrants ) // For each quadrant in the shape (and each neighbor which overlaps with said quadrant)
        {
            const auto & neighborShape = isomLinks[neighbors[quadrant].isomValue];
            auto neighborTerrainType = neighborShape.terrainType;
            auto neighborLinkId = neighbors[quadrant].linkId;
            auto quadrantLinkId = shapeLinks.getLinkId(quadrant);

            if ( neighborLinkId == quadrantLinkId && (quadrantLinkId < Sc::Isom::LinkId::OnlyMatchSameType || terrainType == neighborTerrainType) )
                ++totalMatches;
            else if ( neighbors[quadrant].modified ) // There was no match with a neighbor that was already modified, so this isomValue can't be valid
                return uint16_t(0);
        }
        return totalMatches;
    }
    void searchForBestMatch(uint16_t startingTerrainType, IsomNeighbors & neighbors, Chk::IsomCache & cache) const
    {
        bool searchUntilHigherTerrainType = startingTerrainType == cache.terrainTypes.size()/2+1; // The final search always searches until end or higher types
        bool searchUntilEnd = startingTerrainType == 0; // If startingTerrainType is zero, the whole table after start must be searched

        uint16_t isomValue = cache.getTerrainTypeIsomValue(startingTerrainType);
        for ( ; isomValue < cache.isomLinks.size(); ++isomValue )
        {
            auto terrainType = cache.isomLinks[isomValue].terrainType;
            if ( !searchUntilEnd && terrainType != startingTerrainType && (!searchUntilHigherTerrainType || terrainType > startingTerrainType) )
                break; // Do not search the rest of the table

            auto matchCount = countNeighborMatches(cache.isomLinks[isomValue], neighbors, cache.isomLinks);
            if ( matchCount > neighbors.bestMatch.matchCount )
                neighbors.bestMatch = {isomValue, matchCount};
        }
    }
    std::optional<uint16_t> findBestMatchIsomValue(Chk::IsomDiamond isomDiamond, Chk::IsomCache & cache) const
    {
        IsomNeighbors neighbors {};
        loadNeighborInfo(isomDiamond, neighbors, cache.isomLinks);

        uint16_t prevIsomValue = getCentralIsomValue(isomDiamond);
        if ( prevIsomValue < cache.isomLinks.size() )
        {
            uint8_t prevTerrainType = cache.isomLinks[prevIsomValue].terrainType; // Y = maxOfFour, x = prevTerrainType
            uint16_t mappedTerrainType = cache.terrainTypeMap[size_t(neighbors.maxModifiedOfFour)*cache.terrainTypes.size() + size_t(prevTerrainType)];
            searchForBestMatch(mappedTerrainType, neighbors, cache);
        }
        searchForBestMatch(uint16_t(neighbors.maxModifiedOfFour), neighbors, cache);
        searchForBestMatch(uint16_t(cache.terrainTypes.size()/2 + 1), neighbors, cache);

        if ( neighbors.bestMatch.isomValue == prevIsomValue ) // This ISOM diamond was already the best possible value
            return std::nullopt;
        else
            return neighbors.bestMatch.isomValue;
    }
    void radiallyUpdateTerrain(bool undoable, std::deque<Chk::IsomDiamond> & diamondsToUpdate, Chk::IsomCache & cache)
    {
        while ( !diamondsToUpdate.empty() )
        {
            Chk::IsomDiamond isomDiamond = diamondsToUpdate.front();
            diamondsToUpdate.pop_front();
            if ( diamondNeedsUpdate(isomDiamond) && !getIsomRect(isomDiamond).isVisited() )
            {
                isomRectAt(isomDiamond).setVisited();
                cache.changedArea.expandToInclude(isomDiamond.x, isomDiamond.y);
                if ( auto bestMatch = findBestMatchIsomValue(isomDiamond, cache) )
                {
                    if ( *bestMatch != 0 )
                        setDiamondIsomValues(isomDiamond, *bestMatch, undoable, cache);

                    for ( auto i : Chk::IsomDiamond::neighbors )
                    {
                        Chk::IsomDiamond neighbor = isomDiamond.getNeighbor(i);
                        if ( diamondNeedsUpdate(neighbor) )
                            diamondsToUpdate.push_back({neighbor.x, neighbor.y});
                    }
                }
            }
        }
    }

    void updateTileFromIsom(Chk::IsomDiamond isomDiamond, Chk::IsomCache & cache)
    {
        if ( isomDiamond.x+1 >= cache.isomWidth || isomDiamond.y+1 >= cache.isomHeight )
            return;

        size_t leftTileX = 2*isomDiamond.x;
        size_t rightTileX = leftTileX+1;

        size_t totalConnections = cache.tileGroups.size();

        uint32_t isomRectHash = getIsomRect(isomDiamond).getHash(cache.isomLinks);
        auto foundPotentialGroups = cache.hashToTileGroup->find(isomRectHash);
        if ( foundPotentialGroups != cache.hashToTileGroup->end() )
        {
            const std::vector<uint16_t> & potentialGroups = foundPotentialGroups->second;
            uint16_t destTileGroup = potentialGroups[0];

            // Lookup the isom group for this row using the above rows stack-bottom connection
            if ( isomDiamond.y > 0 )
            {
                auto aboveTileGroup = Sc::Terrain::getTileGroup(getTileValue(leftTileX, isomDiamond.y-1));
                if ( aboveTileGroup < cache.tileGroups.size() )
                {
                    uint16_t tileGroupBottom = cache.tileGroups[aboveTileGroup].stackConnections.bottom;
                    for ( size_t i=0; i<potentialGroups.size(); ++i )
                    {
                        if ( cache.tileGroups[potentialGroups[i]].stackConnections.top == tileGroupBottom )
                        {
                            destTileGroup = potentialGroups[i];
                            break;
                        }
                    }
                }
            }

            uint16_t destSubTile = cache.getRandomSubtile(destTileGroup) % 16;
            setTileValue(leftTileX, isomDiamond.y, 16*destTileGroup + destSubTile);
            setTileValue(rightTileX, isomDiamond.y, 16*(destTileGroup+1) + destSubTile);

            // Find the top row of the tile-group stack (note: this is a tad performance sensitive, consider pre-linking stacks)
            size_t stackTopY = isomDiamond.y;
            auto curr = Sc::Terrain::getTileGroup(getTileValue(leftTileX, stackTopY));
            for ( ; stackTopY > 0 && curr < totalConnections && cache.tileGroups[curr].stackConnections.top != 0; --stackTopY )
            {
                auto above = Sc::Terrain::getTileGroup(getTileValue(leftTileX, stackTopY-1));
                if ( above >= totalConnections || cache.tileGroups[curr].stackConnections.top != cache.tileGroups[above].stackConnections.bottom )
                    break;

                curr = above;
            }

            setTileValue(leftTileX, stackTopY, 16*Sc::Terrain::getTileGroup(getTileValue(leftTileX, stackTopY)) + destSubTile);
            setTileValue(rightTileX, stackTopY, 16*Sc::Terrain::getTileGroup(getTileValue(rightTileX, stackTopY)) + destSubTile);

            // Set tile values for the rest of the stack
            for ( size_t y=stackTopY+1; y<tileHeight; ++y )
            {
                auto tileGroup = Sc::Terrain::getTileGroup(getTileValue(leftTileX, y-1));
                auto nextTileGroup = Sc::Terrain::getTileGroup(getTileValue(leftTileX, y));

                if ( tileGroup >= cache.tileGroups.size() || nextTileGroup >= cache.tileGroups.size() ||
                    cache.tileGroups[tileGroup].stackConnections.bottom == 0 || cache.tileGroups[nextTileGroup].stackConnections.top == 0 )
                {
                    break;
                }

                uint16_t bottomConnection = cache.tileGroups[tileGroup].stackConnections.bottom;
                uint16_t leftTileGroup = Sc::Terrain::getTileGroup(getTileValue(leftTileX, y));
                uint16_t rightTileGroup = Sc::Terrain::getTileGroup(getTileValue(rightTileX, y));
                if ( bottomConnection != cache.tileGroups[nextTileGroup].stackConnections.top )
                {
                    isomRectHash = getIsomRect({isomDiamond.x, y}).getHash(cache.isomLinks);

                    auto foundPotentialGroups = cache.hashToTileGroup->find(isomRectHash);
                    if ( foundPotentialGroups != cache.hashToTileGroup->end() )
                    {
                        const std::vector<uint16_t> & potentialGroups = foundPotentialGroups->second;
                        for ( size_t i=0; i<potentialGroups.size(); ++i )
                        {
                            if ( cache.tileGroups[potentialGroups[i]].stackConnections.top == bottomConnection )
                            {
                                leftTileGroup = potentialGroups[i];
                                rightTileGroup = leftTileGroup + 1;
                                break;
                            }
                        }
                    }
                }

                setTileValue(leftTileX, y, 16*leftTileGroup + destSubTile);
                setTileValue(rightTileX, y, 16*rightTileGroup + destSubTile);
            }
        }
        else
        {
            setTileValue(leftTileX, isomDiamond.y, 0);
            setTileValue(rightTileX, isomDiamond.y, 0);
        }
    }
};

#endif