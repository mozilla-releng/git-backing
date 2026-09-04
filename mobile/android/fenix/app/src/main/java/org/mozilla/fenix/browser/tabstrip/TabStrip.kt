/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.tabstrip

import android.annotation.SuppressLint
import android.graphics.Bitmap
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.BoxWithConstraintsScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.systemGestureExclusion
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.traversalIndex
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Devices
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.coerceIn
import androidx.compose.ui.unit.dp
import androidx.core.text.BidiFormatter
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.feature.tabs.TabsUseCases
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.components
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.compose.HorizontalFadingEdgeBox
import org.mozilla.fenix.compose.ext.isItemPartiallyVisible
import org.mozilla.fenix.tabgroups.storage.repository.TabGroupRepository
import org.mozilla.fenix.tabgroups.strip.TabGroupStripConfig
import org.mozilla.fenix.tabstray.browser.compose.ReorderableDragItemContainer
import org.mozilla.fenix.tabstray.browser.compose.createListReorderState
import org.mozilla.fenix.tabstray.browser.compose.detectListPressAndDrag
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import org.mozilla.fenix.theme.ThemedValue
import org.mozilla.fenix.theme.ThemedValueProvider
import org.mozilla.fenix.utils.Settings
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.GleanMetrics.TabStrip as TabStripMetrics

private val minTabStripItemWidth = 130.dp
private val maxTabStripItemWidth = 280.dp
private val tabItemHeight = 40.dp
private val spaceBetweenTabs = 4.dp
private val tabStripListContentStartPadding = 8.dp
private val titleFadeWidth = 16.dp
private val groupNameChipHeight = tabItemHeight
private val groupNameChipMaxWidth = 120.dp

// Pill on three corners; the bottom start corner is tucked in where the underline begins.
private val groupNameChipShape = RoundedCornerShape(
    topStart = tabItemHeight / 2,
    topEnd = tabItemHeight / 2,
    bottomEnd = tabItemHeight / 2,
    bottomStart = 6.dp,
)
private val groupCountBadgeSize = 30.dp
private const val GROUP_COUNT_BADGE_ALPHA = 0.1f
private val groupUnderlineHeight = 2.dp
private val groupChipTabGap = 8.dp
private val groupUnderlineTopGap = 4.dp
// Matches the default `tween()` used for lazy item placement in [ReorderableDragItemContainer] so
// a group's width animation and its neighbours' slide stay in lockstep instead of overlapping.
private val groupPresenceAnimMs = 300
private val groupUnderlineFadeMs = 120

/** How an open tab group is shown in the tablet tab strip. */
private enum class TabStripGroupDisplayMode {
    /** Name chip + all member tabs + full underline. */
    Expanded,

    /** Name chip + selected tab + "+N" + underline under that span. */
    CollapsedWithActive,

    /** Name chip only + short underline, for a collapsed group with no selected tab. */
    FullyCollapsed,
}

private val tabStripIconSize
    @Composable
    get() = FirefoxTheme.layout.size.static200

/**
 * Top level composable for the tabs strip.
 *
 * @param isSelectDisabled Whether or not the tabs can be shown as selected.
 * @param showTabCounterButton Show the tab counter button in the tabs strip when true.
 * @param tabStripColors The colors to use for the tabs strip.
 * @param browserStore The [BrowserStore] instance used to observe tabs state.
 * @param appStore The [AppStore] instance used to observe browsing mode.
 * @param tabsUseCases The [TabsUseCases] instance to perform tab actions.
 * @param onAddTabClick Invoked when the add tab button is clicked.
 * @param onCloseTabClick Invoked when a tab is closed.
 * @param onLastTabClose Invoked when the last remaining open tab is closed.
 * @param onSelectedTabClick Invoked when a tab is selected.
 * @param onTabCounterClick Invoked when tab counter is clicked.
 */
@Composable
fun TabStrip(
    isSelectDisabled: Boolean = false,
    showTabCounterButton: Boolean = true,
    tabStripColors: TabStripColors = TabStripColors.default(),
    browserStore: BrowserStore = components.core.store,
    appStore: AppStore = components.appStore,
    tabsUseCases: TabsUseCases = components.useCases.tabsUseCases,
    tabGroupRepository: TabGroupRepository = components.core.tabGroupRepository,
    settings: Settings = components.settings,
    onAddTabClick: () -> Unit,
    onCloseTabClick: (isPrivate: Boolean) -> Unit,
    onLastTabClose: (isPrivate: Boolean) -> Unit,
    onSelectedTabClick: (url: String) -> Unit,
    onTabCounterClick: () -> Unit,
) {
    val isPossiblyPrivateMode by remember { appStore.stateFlow.map { it.mode.isPrivate } }
        .collectAsState(initial = false)
    val tabGroupsEnabled = TabGroupStripConfig.isEnabled && settings.tabGroupsEnabled
    val state by remember(isSelectDisabled, isPossiblyPrivateMode, tabGroupsEnabled) {
        observeTabStripState(
            browserStore = browserStore,
            tabGroupRepository = tabGroupRepository,
            tabGroupsEnabled = tabGroupsEnabled,
            isSelectDisabled = isSelectDisabled,
            isPossiblyPrivateMode = isPossiblyPrivateMode,
            onAddTabClick = onAddTabClick,
            tabsUseCases = tabsUseCases,
            onLastTabClose = onLastTabClose,
            onCloseTabClick = onCloseTabClick,
        )
    }.collectAsState(initial = TabStripState.initial)

    TabStripContent(
        state = state,
        showTabCounterButton = showTabCounterButton,
        colors = tabStripColors,
        onAddTabClick = {
            onAddTabClick()
            TabStripMetrics.newTabTapped.record()
        },
        onCloseTabClick = { tabId, isPrivate ->
            closeTab(
                numberOfTabs = state.tabs.size,
                isPrivate = isPrivate,
                tabsUseCases = tabsUseCases,
                tabId = tabId,
                onLastTabClose = onLastTabClose,
                onCloseTabClick = onCloseTabClick,
            )
        },
        onSelectedTabClick = { tabId, url ->
            tabsUseCases.selectTab(tabId)
            onSelectedTabClick(url)
            TabStripMetrics.selectTab.record()
        },
        onMove = { tabId, targetId, placeAfter ->
            if (tabId != targetId) {
                tabsUseCases.moveTabs(listOf(tabId), targetId, placeAfter)
            }
        },
        onTabCounterClick = onTabCounterClick,
    )
}

private fun observeTabStripState(
    browserStore: BrowserStore,
    tabGroupRepository: TabGroupRepository,
    tabGroupsEnabled: Boolean,
    isSelectDisabled: Boolean,
    isPossiblyPrivateMode: Boolean,
    onAddTabClick: () -> Unit,
    tabsUseCases: TabsUseCases,
    onLastTabClose: (isPrivate: Boolean) -> Unit,
    onCloseTabClick: (isPrivate: Boolean) -> Unit,
) = if (!tabGroupsEnabled) {
    browserStore.stateFlow.map { browserState ->
        browserState.toTabStripState(
            isSelectDisabled = isSelectDisabled,
            isPossiblyPrivateMode = isPossiblyPrivateMode,
            addTab = onAddTabClick,
            closeTab = { isPrivate, numberOfTabs ->
                browserState.selectedTabId?.let { selectedTabId ->
                    closeTab(
                        numberOfTabs = numberOfTabs,
                        isPrivate = isPrivate,
                        tabsUseCases = tabsUseCases,
                        tabId = selectedTabId,
                        onLastTabClose = onLastTabClose,
                        onCloseTabClick = onCloseTabClick,
                    )
                }
            },
        )
    }
} else {
    combine(
        browserStore.stateFlow,
        tabGroupRepository.tabGroupDataFlow,
    ) { browserState, tabGroupData ->
        browserState.toTabStripState(
            isSelectDisabled = isSelectDisabled,
            isPossiblyPrivateMode = isPossiblyPrivateMode,
            addTab = onAddTabClick,
            closeTab = { isPrivate, numberOfTabs ->
                browserState.selectedTabId?.let { selectedTabId ->
                    closeTab(
                        numberOfTabs = numberOfTabs,
                        isPrivate = isPrivate,
                        tabsUseCases = tabsUseCases,
                        tabId = selectedTabId,
                        onLastTabClose = onLastTabClose,
                        onCloseTabClick = onCloseTabClick,
                    )
                }
            },
            tabGroupData = tabGroupData,
        )
    }
}

@Composable
private fun TabStripContent(
    state: TabStripState,
    colors: TabStripColors,
    showTabCounterButton: Boolean = true,
    onAddTabClick: () -> Unit,
    onCloseTabClick: (id: String, isPrivate: Boolean) -> Unit,
    onSelectedTabClick: (tabId: String, url: String) -> Unit,
    onMove: (tabId: String, targetId: String, placeAfter: Boolean) -> Unit,
    onTabCounterClick: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(dimensionResource(R.dimen.tab_strip_height))
            .background(brush = colors.backgroundBrush)
            .systemGestureExclusion(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Row(
            modifier = Modifier.weight(1f, fill = false),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            TabsList(
                state = state,
                modifier = Modifier.weight(1f, fill = false),
                tabItemBackgroundColors = colors.tabItemBackgroundColors,
                onCloseTabClick = onCloseTabClick,
                onSelectedTabClick = onSelectedTabClick,
                onMove = onMove,
            )

            IconButton(onClick = onAddTabClick) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_plus_24),
                    tint = MaterialTheme.colorScheme.onSurface,
                    contentDescription = stringResource(R.string.add_tab),
                )
            }
        }

        if (showTabCounterButton) {
            TabStripTabCounterButton(
                tabCount = state.tabs.size,
                size = dimensionResource(R.dimen.tab_strip_height),
                menuItems = state.menuItems,
                privacyBadgeVisible = state.isPrivateMode,
                onClick = onTabCounterClick,
            )
        }
    }
}

// There is a bug with `BoxWithConstraints` where it flags the `BoxWithConstraintsScope` being unused
// even though it's being used implicitly below via the `maxWidth` property of `BoxWithConstraintsScope`.
@SuppressLint("UnusedBoxWithConstraintsScope")
@Composable
private fun TabsList(
    state: TabStripState,
    tabItemBackgroundColors: TabStripColors.TabColors,
    modifier: Modifier = Modifier,
    onCloseTabClick: (id: String, isPrivate: Boolean) -> Unit,
    onSelectedTabClick: (tabId: String, url: String) -> Unit,
    onMove: (tabId: String, targetId: String, placeAfter: Boolean) -> Unit,
) {
    BoxWithConstraints(modifier = modifier) {
        val listState = rememberLazyListState()
        val tabWidth = calculateTabWidth(state.tabs.size)
        // Collapse is only ever toggled by the user; selecting a tab in another group must not
        // collapse the groups around it.
        val collapsedGroupIds by TabStripGroupCollapseState.collapsedGroupIds.collectAsState()
        val openGroupIds = remember(state.entries) {
            state.entries.filterIsInstance<TabStripEntry.Group>().map { it.id }.toSet()
        }
        LaunchedEffect(openGroupIds) {
            TabStripGroupCollapseState.retainOnly(openGroupIds)
        }
        val entryTabIds = remember(state.entries) {
            state.entries.associate { entry ->
                entry.key to when (entry) {
                    is TabStripEntry.Tab -> entry.item.id
                    is TabStripEntry.Group -> entry.tabs.first().id
                }
            }
        }

        val reorderState = createListReorderState(
            listState = listState,
            onMove = { movedTab, adjacentTab ->
                val movedId = entryTabIds[movedTab.key as String] ?: return@createListReorderState
                val targetId = entryTabIds[adjacentTab.key as String] ?: return@createListReorderState
                onMove(
                    movedId,
                    targetId,
                    movedTab.index < adjacentTab.index,
                )
            },
            ignoredItems = emptyList(),
        )

        LazyRow(
            modifier = Modifier
                .fillMaxHeight()
                .detectListPressAndDrag(
                    reorderState = reorderState,
                    listState = listState,
                    shouldLongPressToDrag = true,
                )
                .selectableGroup(),
            state = listState,
            contentPadding = PaddingValues(start = tabStripListContentStartPadding),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            itemsIndexed(
                items = state.entries,
                key = { _, item -> item.key },
            ) { index, entry ->
                ReorderableDragItemContainer(
                    state = reorderState,
                    key = entry.key,
                    position = index,
                ) {
                    when (entry) {
                        is TabStripEntry.Tab -> {
                            TabItem(
                                state = entry.item,
                                onCloseTabClick = onCloseTabClick,
                                onSelectedTabClick = onSelectedTabClick,
                                backgroundColors = tabItemBackgroundColors,
                                modifier = Modifier
                                    .padding(
                                        end = spaceBetweenTabs,
                                        bottom = groupUnderlineTopGap + groupUnderlineHeight,
                                    )
                                    .width(tabWidth)
                                    .thenConditional(
                                        modifier = Modifier.semantics { traversalIndex = -1f },
                                        predicate = { entry.item.isSelected },
                                    ),
                            )
                        }
                        is TabStripEntry.Group -> {
                            val hasSelectedTab = entry.tabs.any { it.isSelected }
                            val displayMode = when {
                                entry.id !in collapsedGroupIds -> TabStripGroupDisplayMode.Expanded
                                hasSelectedTab -> TabStripGroupDisplayMode.CollapsedWithActive
                                else -> TabStripGroupDisplayMode.FullyCollapsed
                            }
                            TabStripGroupBlock(
                                group = entry,
                                displayMode = displayMode,
                                tabWidth = tabWidth,
                                backgroundColors = tabItemBackgroundColors,
                                onCloseTabClick = onCloseTabClick,
                                onSelectedTabClick = onSelectedTabClick,
                                onGroupChipClick = { TabStripGroupCollapseState.toggle(entry.id) },
                                onCountChipClick = { TabStripGroupCollapseState.expand(entry.id) },
                                modifier = Modifier
                                    .padding(end = spaceBetweenTabs)
                                    .thenConditional(
                                        modifier = Modifier.semantics { traversalIndex = -1f },
                                        predicate = { entry.tabs.any { it.isSelected } },
                                    ),
                            )
                        }
                    }
                }
            }
        }

        if (state.tabs.isNotEmpty()) {
            // When a new tab is added, scroll to the end of the list. This is done here instead of
            // in onCloseTabClick so this acts on state change which can occur from any other
            // place e.g. tabs tray.
            LaunchedEffect(state.tabs.last().id) {
                listState.scrollToItem(state.entries.size)
            }

            // When a tab is selected, scroll to the selected tab. This is done here instead of
            // in onSelectedTabClick so this acts on state change which can occur from any other
            // place e.g. tabs tray.
            val selectedTab = state.tabs.firstOrNull { it.isSelected }
            val selectedEntryKey = state.entries.firstOrNull { entry ->
                when (entry) {
                    is TabStripEntry.Tab -> entry.item.id == selectedTab?.id
                    is TabStripEntry.Group -> entry.tabs.any { it.id == selectedTab?.id }
                }
            }?.key
            LaunchedEffect(selectedTab?.id) {
                if (selectedTab != null && selectedEntryKey != null) {
                    val selectedItemInfo =
                        listState.layoutInfo.visibleItemsInfo.firstOrNull { it.key == selectedEntryKey }

                    if (selectedItemInfo == null || listState.isItemPartiallyVisible(selectedItemInfo)) {
                        val index = state.entries.indexOfFirst { it.key == selectedEntryKey }
                        if (index >= 0) {
                            listState.animateScrollToItem(index)
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun TabStripGroupBlock(
    group: TabStripEntry.Group,
    displayMode: TabStripGroupDisplayMode,
    tabWidth: Dp,
    backgroundColors: TabStripColors.TabColors,
    onCloseTabClick: (id: String, isPrivate: Boolean) -> Unit,
    onSelectedTabClick: (tabId: String, url: String) -> Unit,
    onGroupChipClick: () -> Unit,
    onCountChipClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val groupColor = group.theme.primary
    val selectedTab = group.tabs.firstOrNull { it.isSelected }
    val visibleTabs = when (displayMode) {
        TabStripGroupDisplayMode.Expanded -> group.tabs
        TabStripGroupDisplayMode.CollapsedWithActive -> listOfNotNull(selectedTab)
        TabStripGroupDisplayMode.FullyCollapsed -> emptyList()
    }
    val hiddenCount = (group.tabs.size - visibleTabs.size).coerceAtLeast(0)
    val showCountChip =
        displayMode == TabStripGroupDisplayMode.CollapsedWithActive && hiddenCount > 0

    val underlineAlpha = remember { Animatable(1f) }
    var previousDisplayMode by remember { mutableStateOf<TabStripGroupDisplayMode?>(null) }
    LaunchedEffect(displayMode) {
        val previous = previousDisplayMode
        previousDisplayMode = displayMode
        val fade = tween<Float>(
            durationMillis = groupUnderlineFadeMs,
            easing = FastOutSlowInEasing,
        )
        when {
            // Only the name chip is showing, so there is no tab span to underline.
            displayMode == TabStripGroupDisplayMode.FullyCollapsed -> {
                if (previous == null) {
                    underlineAlpha.snapTo(0f)
                } else {
                    underlineAlpha.animateTo(targetValue = 0f, animationSpec = fade)
                }
            }
            previous == null || previous == displayMode -> underlineAlpha.snapTo(1f)
            else -> {
                // Fade in as the line starts to grow with animateContentSize.
                underlineAlpha.snapTo(0f)
                underlineAlpha.animateTo(targetValue = 1f, animationSpec = fade)
            }
        }
    }

    Column(
        // Clip and draw outside of `animateContentSize` so the block never paints wider than the
        // width it reports to the strip, and so the underline grows and shrinks with it.
        modifier = modifier
            .clipToBounds()
            .drawBehind {
                val stroke = groupUnderlineHeight.toPx()
                val top = size.height - stroke
                drawRoundRect(
                    color = groupColor.copy(alpha = groupColor.alpha * underlineAlpha.value),
                    topLeft = Offset(0f, top),
                    size = Size(size.width, stroke),
                    cornerRadius = CornerRadius(stroke / 2f, stroke / 2f),
                )
            }
            .animateContentSize(
                animationSpec = tween(
                    durationMillis = groupPresenceAnimMs,
                    easing = FastOutSlowInEasing,
                ),
            )
            .padding(bottom = groupUnderlineTopGap + groupUnderlineHeight),
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
        ) {
            TabStripGroupNameChip(
                title = group.title,
                theme = group.theme,
                onClick = onGroupChipClick,
            )
            if (visibleTabs.isNotEmpty() || showCountChip) {
                Spacer(modifier = Modifier.width(groupChipTabGap))
            }
            visibleTabs.forEachIndexed { index, tab ->
                TabItem(
                    state = tab,
                    onCloseTabClick = onCloseTabClick,
                    onSelectedTabClick = onSelectedTabClick,
                    backgroundColors = backgroundColors,
                    selectedBorderColor = groupColor,
                    modifier = Modifier
                        .width(tabWidth)
                        .then(
                            if (index < visibleTabs.lastIndex || showCountChip) {
                                Modifier.padding(end = spaceBetweenTabs)
                            } else {
                                Modifier
                            },
                        ),
                )
            }
            if (showCountChip) {
                TabStripGroupCountBadge(
                    count = hiddenCount,
                    theme = group.theme,
                    onClick = onCountChipClick,
                )
            }
        }
    }
}

@Composable
private fun TabStripGroupNameChip(
    title: String,
    theme: TabGroupTheme,
    onClick: () -> Unit,
) {
    Box(
        modifier = Modifier
            .height(groupNameChipHeight)
            .widthIn(max = groupNameChipMaxWidth)
            .clip(groupNameChipShape)
            .background(theme.primary)
            .clickable(onClick = onClick)
            .padding(horizontal = 12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = title,
            color = theme.onPrimary,
            style = FirefoxTheme.typography.body2,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            softWrap = false,
        )
    }
}

@Composable
private fun TabStripGroupCountBadge(
    count: Int,
    theme: TabGroupTheme,
    onClick: () -> Unit,
) {
    Box(
        modifier = Modifier
            .defaultMinSize(
                minWidth = groupCountBadgeSize,
                minHeight = groupCountBadgeSize,
            )
            .clip(CircleShape)
            .background(theme.primary.copy(alpha = GROUP_COUNT_BADGE_ALPHA))
            .clickable(onClick = onClick)
            .padding(horizontal = 6.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = "+$count",
            color = theme.primary,
            style = FirefoxTheme.typography.body2,
            maxLines = 1,
            softWrap = false,
        )
    }
}

/**
 * Calculates the width of each tab item based on available width and the number of tabs.
 *
 * @param tabCount The number of tabs to display.
 * @return The calculated width for each tab, constrained between min and max tab widths.
 */
@Composable
private fun BoxWithConstraintsScope.calculateTabWidth(tabCount: Int): Dp {
    val availableWidth = maxWidth - tabStripListContentStartPadding
    return ((availableWidth / tabCount) - spaceBetweenTabs).coerceIn(
        minimumValue = minTabStripItemWidth,
        maximumValue = maxTabStripItemWidth,
    )
}

@Composable
@Suppress("LongMethod", "CognitiveComplexMethod")
private fun TabItem(
    state: TabStripItem,
    modifier: Modifier = Modifier,
    backgroundColors: TabStripColors.TabColors,
    selectedBorderColor: Color? = null,
    onCloseTabClick: (id: String, isPrivate: Boolean) -> Unit,
    onSelectedTabClick: (id: String, url: String) -> Unit,
) {
    val backgroundColor = backgroundColors.get(state.isSelected)
    val closeTabLabel = stringResource(R.string.close_tab)

    TabStripCard(
        modifier = modifier.height(tabItemHeight),
        backgroundColor = backgroundColor,
        border = if (state.isSelected) {
            BorderStroke(
                width = 1.dp,
                brush = if (selectedBorderColor != null) {
                    SolidColor(selectedBorderColor)
                } else {
                    FirefoxTheme.gradients.tabOutline.brush
                },
            )
        } else {
            null
        },
    ) {
        Row(
            modifier = Modifier
                .fillMaxSize()
                .clickable { onSelectedTabClick(state.id, state.url) }
                .semantics {
                    role = Role.Tab
                    selected = state.isSelected
                    customActions = listOf(
                        CustomAccessibilityAction(
                            label = closeTabLabel,
                        ) {
                            onCloseTabClick(state.id, state.isPrivate)
                            true
                        },
                    )
                },
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Row(
                modifier = Modifier.weight(1f),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                // This makes sure that isRtl is only calculated when the title changes.
                val isTitleRtl = remember(state.title) {
                    BidiFormatter.getInstance().isRtl(state.title)
                }

                Spacer(modifier = Modifier.size(8.dp))

                TabStripIcon(
                    url = state.url,
                    icon = state.icon,
                )

                Spacer(modifier = Modifier.size(8.dp))

                HorizontalFadingEdgeBox(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight(),
                    fadeWidth = titleFadeWidth,
                    backgroundColor = backgroundColor,
                    isContentRtl = isTitleRtl,
                ) {
                    Text(
                        text = state.title,
                        modifier = Modifier.align(Alignment.CenterStart),
                        color = MaterialTheme.colorScheme.onSurface,
                        softWrap = false,
                        maxLines = 1,
                        style = FirefoxTheme.typography.body2,
                    )
                }
            }

            if (state.isCloseButtonVisible) {
                IconButton(
                    onClick = { onCloseTabClick(state.id, state.isPrivate) },
                    modifier = if (state.isSelected) {
                        Modifier.semantics {}
                    } else {
                        Modifier.clearAndSetSemantics {}
                    },
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_cross_20),
                        tint = if (state.isSelected) {
                            MaterialTheme.colorScheme.onSurface
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        },
                        contentDescription = stringResource(
                            id = R.string.close_tab_title,
                            state.title,
                        ),
                    )
                }
            } else {
                Spacer(modifier = Modifier.size(8.dp))
            }
        }
    }
}

/**
 * Displays the icon for a tab in the tab strip.
 *
 * @param url The URL of the tab, used to generate a favicon if no icon is provided.
 * @param icon The tab's favicon bitmap, if available.
 */
@Composable
private fun TabStripIcon(
    url: String,
    icon: Bitmap?,
) {
    Box(
        modifier = Modifier
            .size(tabStripIconSize)
            .clip(CircleShape),
        contentAlignment = Alignment.Center,
    ) {
        if (icon != null && !icon.isRecycled) {
            Image(
                bitmap = icon.asImageBitmap(),
                contentDescription = null,
                modifier = Modifier
                    .size(tabStripIconSize)
                    .clip(CircleShape),
            )
        } else if (url == ABOUT_HOME_URL) {
            Favicon(
                imageResource = R.drawable.ic_firefox,
                size = tabStripIconSize,
            )
        } else {
            Favicon(
                url = url,
                size = tabStripIconSize,
            )
        }
    }
}

private fun closeTab(
    numberOfTabs: Int,
    isPrivate: Boolean,
    tabsUseCases: TabsUseCases,
    tabId: String,
    onLastTabClose: (isPrivate: Boolean) -> Unit,
    onCloseTabClick: (isPrivate: Boolean) -> Unit,
) {
    if (numberOfTabs == 1) {
        onLastTabClose(isPrivate)
    }
    tabsUseCases.removeTab(tabId)
    onCloseTabClick(isPrivate)
    TabStripMetrics.closeTab.record()
}

private class TabUIStateParameterProvider : ThemedValueProvider<TabStripState>(
    sequenceOf(
        TabStripState(
            listOf(
                TabStripItem(
                    id = "1",
                    title = "Tab 1",
                    url = "https://www.mozilla.org",
                    isPrivate = false,
                    isSelected = false,
                ),
                TabStripItem(
                    id = "2",
                    title = "Tab 2 with a very long title that should be truncated",
                    url = "https://www.mozilla.org",
                    isPrivate = false,
                    isSelected = false,
                ),
                TabStripItem(
                    id = "3",
                    title = "Selected tab",
                    url = "https://www.mozilla.org",
                    isPrivate = false,
                    isSelected = true,
                ),
                TabStripItem(
                    id = "p1",
                    title = "Private tab 1",
                    url = "https://www.mozilla.org",
                    isPrivate = true,
                    isSelected = false,
                ),
                TabStripItem(
                    id = "p2",
                    title = "Private selected tab",
                    url = "https://www.mozilla.org",
                    isPrivate = true,
                    isSelected = true,
                ),
            ),
            isPrivateMode = false,
            tabCounterMenuItems = emptyList(),
        ),
    ),
)

@Preview(device = Devices.PIXEL_TABLET)
@Composable
private fun TabStripPreview(
    @PreviewParameter(TabUIStateParameterProvider::class) tabStripState: ThemedValue<TabStripState>,
) {
    FirefoxTheme(tabStripState.theme) {
        TabStripContentPreview(
            tabStripState.value.tabs.filter {
                if (tabStripState.theme == Theme.Private) {
                    it.isPrivate
                } else {
                    !it.isPrivate
                }
            },
        )
    }
}

@Composable
private fun TabStripContentPreview(tabs: List<TabStripItem>) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .height(dimensionResource(id = R.dimen.tab_strip_height)),
    ) {
        TabStripContent(
            state = TabStripState(
                tabs = tabs,
                isPrivateMode = false,
                tabCounterMenuItems = emptyList(),
            ),
            colors = TabStripColors.default(),
            onAddTabClick = {},
            onCloseTabClick = { _, _ -> },
            onSelectedTabClick = { _, _ -> },
            onMove = { _, _, _ -> },
            onTabCounterClick = {},
        )
    }
}

@Preview(device = Devices.PIXEL_TABLET)
@Composable
private fun TabStripPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    val browserStore = BrowserStore()

    FirefoxTheme(theme) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .height(dimensionResource(id = R.dimen.tab_strip_height)),
        ) {
            TabStrip(
                appStore = AppStore(),
                browserStore = browserStore,
                tabsUseCases = TabsUseCases(browserStore),
                onAddTabClick = {
                    val tab = createTab(
                        url = "www.example.com",
                    )
                    browserStore.dispatch(TabListAction.AddTabAction(tab))
                },
                onLastTabClose = {},
                onCloseTabClick = {},
                onSelectedTabClick = {},
                onTabCounterClick = {},
            )
        }
    }
}
