/*-------------------------------------------------------------------------
 * Copyright (C) 2020, 4paradigm
 * transform.cc
 *
 * Author: chenjing
 * Date: 2020/3/13
 *--------------------------------------------------------------------------
 **/
#include "vm/transform.h"
#include <stack>
#include <unordered_map>
#include "vm/physical_op.h"

namespace fesql {
namespace vm {

std::ostream& operator<<(std::ostream& output,
                         const fesql::vm::LogicalOp& thiz) {
    return output << *(thiz.node_);
}
bool TransformLogicalTreeToLogicalGraph(const ::fesql::node::PlanNode* node,
                                        fesql::base::Status& status,  // NOLINT
                                        LogicalGraph& graph) {        // NOLINT
    if (nullptr == node) {
        status.msg = "node is null";
        status.code = common::kOpGenError;
        LOG(WARNING) << status.msg;
        return false;
    }
    std::stack<LogicalOp> stacks;
    LogicalOp op(node);
    graph.AddVertex(op);
    stacks.push(op);
    while (!stacks.empty()) {
        auto source = stacks.top();
        stacks.pop();
        auto& children = source.node_->GetChildren();
        if (!children.empty()) {
            for (auto iter = children.cbegin(); iter != children.cend();
                 iter++) {
                LogicalOp target(*iter);
                if (!graph.IsExist(target)) {
                    stacks.push(target);
                }
                graph.AddEdge(source, target);
            }
        }
    }
    return true;
}

Transform::Transform(node::NodeManager* node_manager, const std::string& db,
                     const std::shared_ptr<Catalog>& catalog)
    : node_manager_(node_manager), db_(db), catalog_(catalog) {}

Transform::~Transform() {}

bool Transform::TransformPlanOp(const ::fesql::node::PlanNode* node,
                                ::fesql::vm::PhysicalOpNode** ouput,
                                ::fesql::base::Status& status) {
    if (nullptr == node || nullptr == ouput) {
        status.msg = "input node or output node is null";
        status.code = common::kPlanError;
        LOG(WARNING) << status.msg;
        return false;
    }
    LogicalOp logical_op = LogicalOp(node);
    auto map_iter = op_map_.find(logical_op);
    // logical plan node already exist
    if (map_iter != op_map_.cend()) {
        *ouput = map_iter->second;
        return true;
    }

    ::fesql::vm::PhysicalOpNode* op = nullptr;
    bool ok = true;
    switch (node->type_) {
        case node::kPlanTypeLimit:
            ok = TransformLimitOp(
                dynamic_cast<const ::fesql::node::LimitPlanNode*>(node), &op,
                status);
            break;
        case node::kPlanTypeProject:
            ok = TransformProjectOp(
                dynamic_cast<const ::fesql::node::ProjectPlanNode*>(node), &op,
                status);
            break;
        case node::kPlanTypeJoin:
            ok = TransformJoinOp(
                dynamic_cast<const ::fesql::node::JoinPlanNode*>(node), &op,
                status);
            break;
        case node::kPlanTypeUnion:
            ok = TransformUnionOp(
                dynamic_cast<const ::fesql::node::UnionPlanNode*>(node), &op,
                status);
            break;
        case node::kPlanTypeGroup:
            ok = TransformGroupOp(
                dynamic_cast<const ::fesql::node::GroupPlanNode*>(node), &op,
                status);
            break;
        case node::kPlanTypeSort:
            ok = TransformSortOp(
                dynamic_cast<const ::fesql::node::SortPlanNode*>(node), &op,
                status);
            break;
        case node::kPlanTypeFilter:
            ok = TransformFilterOp(
                dynamic_cast<const ::fesql::node::FilterPlanNode*>(node), &op,
                status);
            break;
        case node::kPlanTypeTable:
            ok = TransformScanOp(
                dynamic_cast<const ::fesql::node::TablePlanNode*>(node), &op,
                status);
            break;
        case node::kPlanTypeQuery:
            ok = TransformQueryPlan(
                dynamic_cast<const ::fesql::node::QueryPlanNode*>(node), &op,
                status);
            break;
        case node::kPlanTypeRename:
            ok = TransformRenameOp(
                dynamic_cast<const ::fesql::node::RenamePlanNode*>(node), &op,
                status);
            break;
        case node::kPlanTypeDistinct:
            ok = TransformDistinctOp(
                dynamic_cast<const ::fesql::node::DistinctPlanNode*>(node), &op,
                status);
            break;
        default: {
            status.msg = "fail to transform physical plan: can't handle type " +
                         node::NameOfPlanNodeType(node->type_);
            status.code = common::kPlanError;
            LOG(WARNING) << status.msg;
            return false;
        }
    }
    if (!ok) {
        return false;
    }
    op_map_[logical_op] = op;
    *ouput = op;
    return true;
}

bool Transform::TransformLimitOp(const node::LimitPlanNode* node,
                                 PhysicalOpNode** output,
                                 base::Status& status) {
    if (nullptr == node || nullptr == output) {
        status.msg = "input node or output node is null";
        status.code = common::kPlanError;
        LOG(WARNING) << status.msg;
        return false;
    }
    PhysicalOpNode* depend = nullptr;
    if (!TransformPlanOp(node->GetChildren()[0], &depend, status)) {
        return false;
    }
    *output = new PhysicalLimitNode(depend, node->limit_cnt_);
    return true;
}

bool Transform::TransformProjectOp(const node::ProjectPlanNode* node,
                                   PhysicalOpNode** output,
                                   base::Status& status) {
    if (nullptr == node || nullptr == output) {
        status.msg = "input node or output node is null";
        status.code = common::kPlanError;
        LOG(WARNING) << status.msg;
        return false;
    }
    PhysicalOpNode* depend = nullptr;
    if (!TransformPlanOp(node->GetChildren()[0], &depend, status)) {
        return false;
    }

    std::vector<PhysicalOpNode*> ops;
    for (auto iter = node->project_list_vec_.cbegin();
         iter != node->project_list_vec_.cend(); iter++) {
        fesql::node::ProjectListNode* project_list =
            dynamic_cast<fesql::node::ProjectListNode*>(*iter);
        if (project_list->is_window_agg_) {
            PhysicalOpNode* project_op;
            if (!TransformWindowProject(project_list, depend, &project_op,
                                        status)) {
                return false;
            }
            ops.push_back(project_op);
        } else {
            ops.push_back(new PhysicalRowProjectNode(
                depend, &(project_list->GetProjects())));
        }
    }

    if (ops.empty()) {
        status.msg = "fail transform project op: empty projects";
        status.code = common::kPlanError;
        LOG(WARNING) << status.msg;
        return false;
    }

    if (ops.size() == 1) {
        *output = ops[0];
        return true;
    } else {
        auto iter = ops.cbegin();

        PhysicalOpNode* join = new PhysicalJoinNode(
            (*iter), *(++iter), ::fesql::node::kJoinTypeAppend, nullptr);
        iter++;
        for (; iter != ops.cend(); iter++) {
            join = new PhysicalJoinNode(
                join, *iter, ::fesql::node::kJoinTypeAppend, nullptr);
        }
        *output = join;
        return true;
    }
}
bool Transform::TransformWindowProject(const node::ProjectListNode* node,
                                       PhysicalOpNode* depend_node,
                                       PhysicalOpNode** output,
                                       base::Status& status) {
    if (nullptr == node || nullptr == node->w_ptr_ || nullptr == output) {
        status.msg = "project node or window node or output node is null";
        status.code = common::kPlanError;
        LOG(WARNING) << status.msg;
        return false;
    }

    PhysicalOpNode* depend = const_cast<PhysicalOpNode*>(depend_node);
    if (!node->w_ptr_->GetKeys().empty()) {
        PhysicalGroupNode* group_op =
            new PhysicalGroupNode(depend, node->w_ptr_->GetKeys());
        depend = group_op;
    }

    if (!node->w_ptr_->GetOrders().empty()) {
        PhysicalSortNode* sort_op =
            new PhysicalSortNode(depend, node->w_ptr_->GetOrders());
        depend = sort_op;
    }

    if (node->w_ptr_->GetStartOffset() != -1 ||
        node->w_ptr_->GetEndOffset() != -1) {
        PhysicalWindowNode* window_op =
            new PhysicalWindowNode(depend, node->w_ptr_->GetStartOffset(),
                                   node->w_ptr_->GetEndOffset());
        depend = window_op;
    }
    *output = new PhysicalAggrerationNode(depend, &(node->GetProjects()));
    return true;
}
bool Transform::TransformJoinOp(const node::JoinPlanNode* node,
                                PhysicalOpNode** output, base::Status& status) {
    if (nullptr == node || nullptr == output) {
        status.msg = "input node or output node is null";
        status.code = common::kPlanError;
        LOG(WARNING) << status.msg;
        return false;
    }
    PhysicalOpNode* left = nullptr;
    PhysicalOpNode* right = nullptr;
    if (!TransformPlanOp(node->GetChildren()[0], &left, status)) {
        return false;
    }
    if (!TransformPlanOp(node->GetChildren()[1], &right, status)) {
        return false;
    }
    *output =
        new PhysicalJoinNode(left, right, node->join_type_, node->condition_);
    return true;
}
bool Transform::TransformUnionOp(const node::UnionPlanNode* node,
                                 PhysicalOpNode** output,
                                 base::Status& status) {
    if (nullptr == node || nullptr == output) {
        status.msg = "input node or output node is null";
        status.code = common::kPlanError;
        LOG(WARNING) << status.msg;
        return false;
    }
    PhysicalOpNode* left = nullptr;
    PhysicalOpNode* right = nullptr;
    if (!TransformPlanOp(node->GetChildren()[0], &left, status)) {
        return false;
    }
    if (!TransformPlanOp(node->GetChildren()[1], &right, status)) {
        return false;
    }
    *output = new PhysicalUnionNode(left, right, node->is_all);
    return true;
}
bool Transform::TransformGroupOp(const node::GroupPlanNode* node,
                                 PhysicalOpNode** output,
                                 base::Status& status) {
    if (nullptr == node || nullptr == output) {
        status.msg = "input node or output node is null";
        status.code = common::kPlanError;
        LOG(WARNING) << status.msg;
        return false;
    }
    PhysicalOpNode* left = nullptr;
    if (!TransformPlanOp(node->GetChildren()[0], &left, status)) {
        return false;
    }
    *output = new PhysicalGroupNode(left, node->by_list_);
    return true;
}
bool Transform::TransformSortOp(const node::SortPlanNode* node,
                                PhysicalOpNode** output, base::Status& status) {
    if (nullptr == node || nullptr == output) {
        status.msg = "input node or output node is null";
        status.code = common::kPlanError;
        LOG(WARNING) << status.msg;
        return false;
    }
    PhysicalOpNode* left = nullptr;
    if (!TransformPlanOp(node->GetChildren()[0], &left, status)) {
        return false;
    }
    *output = new PhysicalSortNode(left, node->order_list_);
    return true;
}
bool Transform::TransformFilterOp(const node::FilterPlanNode* node,
                                  PhysicalOpNode** output,
                                  base::Status& status) {
    if (nullptr == node || nullptr == output) {
        status.msg = "input node or output node is null";
        status.code = common::kPlanError;
        LOG(WARNING) << status.msg;
        return false;
    }
    PhysicalOpNode* depend = nullptr;
    if (!TransformPlanOp(node->GetChildren()[0], &depend, status)) {
        return false;
    }
    *output = new PhysicalFliterNode(depend, node->condition_);
    return true;
}

bool Transform::TransformScanOp(const node::TablePlanNode* node,
                                PhysicalOpNode** output, base::Status& status) {
    if (nullptr == node || nullptr == output) {
        status.msg = "input node or output node is null";
        status.code = common::kPlanError;
        LOG(WARNING) << status.msg;
        return false;
    }
    auto table = catalog_->GetTable(db_, node->table_);
    if (table) {
        *output = new PhysicalScanTableNode(table);
        return true;
    } else {
        status.msg = "fail to transform scan op: table " + db_ + "." +
                     node->table_ + " not exist!";
        status.code = common::kPlanError;
        LOG(WARNING) << status.msg;
        return false;
    }
}

// return optimized filter condition and scan index name
bool Transform::TryOptimizedFilterCondition(const IndexHint& index_map,
                                            const node::ExprNode* condition,
                                            std::string& index_name,  // NOLINT
                                            node::ExprNode** output) {
    return false;
}
bool Transform::TransformRenameOp(const node::RenamePlanNode* node,
                                  PhysicalOpNode** output,
                                  base::Status& status) {
    if (nullptr == node || nullptr == output) {
        status.msg = "input node or output node is null";
        status.code = common::kPlanError;
        return false;
    }
    PhysicalOpNode* left = nullptr;
    if (!TransformPlanOp(node->GetChildren()[0], &left, status)) {
        return false;
    }
    *output = new PhysicalRenameNode(left, node->table_);
    return true;
}
bool Transform::TransformQueryPlan(const node::QueryPlanNode* node,
                                   PhysicalOpNode** output,
                                   base::Status& status) {
    if (nullptr == node || nullptr == output) {
        status.msg = "input node or output node is null";
        status.code = common::kPlanError;
        return false;
    }
    return TransformPlanOp(node->GetChildren()[0], output, status);
}
bool Transform::TransformDistinctOp(const node::DistinctPlanNode* node,
                                    PhysicalOpNode** output,
                                    base::Status& status) {
    if (nullptr == node || nullptr == output) {
        status.msg = "input node or output node is null";
        status.code = common::kPlanError;
        return false;
    }
    PhysicalOpNode* left = nullptr;
    if (!TransformPlanOp(node->GetChildren()[0], &left, status)) {
        return false;
    }
    *output = new PhysicalDistinctNode(left);
    return true;
}
bool Transform::TransformPhysicalPlan(const ::fesql::node::PlanNode* node,
                                      ::fesql::vm::PhysicalOpNode** output,
                                      ::fesql::base::Status& status) {
    PhysicalOpNode* physical_plan;
    if (!TransformPlanOp(node, &physical_plan, status)) {
        return false;
    }

    for (auto type : passes) {
        switch (type) {
            case kPassGroupByOptimized: {
                GroupByOptimized pass(node_manager_, db_, catalog_);
                PhysicalOpNode* new_op;
                if (pass.Apply(physical_plan, &new_op)) {
                    physical_plan = new_op;
                }
                break;
            }
            default: {
                LOG(WARNING) << "can't not handle pass: "
                             << PhysicalPlanPassTypeName(type);
            }
        }
    }
    *output = physical_plan;
    return true;
}
bool Transform::AddPass(PhysicalPlanPassType type) {
    passes.push_back(type);
    return true;
}

bool GroupByOptimized::Transform(PhysicalOpNode* in, PhysicalOpNode** output) {
    switch (in->type_) {
        case kPhysicalOpGroupBy: {
            PhysicalGroupNode* group_op = dynamic_cast<PhysicalGroupNode*>(in);
            if (kPhysicalOpScan == in->GetProducers()[0]->type_) {
                auto scan_op =
                    dynamic_cast<PhysicalScanNode*>(in->GetProducers()[0]);
                if (kScanTypeTableScan == scan_op->scan_type_) {
                    std::string index_name;
                    const node::ExprListNode* new_groups;
                    if (!TransformGroupExpr(group_op->groups_,
                                            scan_op->table_handler_->GetIndex(),
                                            index_name, &new_groups)) {
                        return false;
                    }

                    PhysicalScanIndexNode* scan_index_op =
                        new PhysicalScanIndexNode(scan_op->table_handler_,
                                                  index_name);
                    // remove node if groups is empty
                    if (new_groups->children_.empty()) {
                        *output = scan_index_op;
                        return true;
                    } else {
                        PhysicalGroupNode* new_group_op =
                            new PhysicalGroupNode(scan_index_op, new_groups);
                        *output = new_group_op;
                        return true;
                    }
                }
            }
            break;
        }
        default: {
            return false;
        }
    }
    return false;
}
bool GroupByOptimized::TransformGroupExpr(const node::ExprListNode* groups,
                                          const IndexHint& index_hint,
                                          std::string& index_name,
                                          const node::ExprListNode** output) {
    std::vector<std::string> columns;
    for (auto group : groups->children_) {
        switch (group->expr_type_) {
            case node::kExprColumnRef:
                columns.push_back(
                    dynamic_cast<node::ColumnRefNode*>(group)->GetColumnName());
                break;
            default: {
                break;
            }
        }
    }

    if (columns.empty()) {
        return false;
    }

    std::vector<bool> bitmap(columns.size(), true);
    if (MatchBestIndex(columns, index_hint, bitmap, index_name)) {
        IndexSt index = index_hint.at(index_name);
        node::ExprListNode* new_groups = node_manager_->MakeExprList();
        std::set<std::string> keys;
        for (auto iter = index.keys.cbegin(); iter != index.keys.cend();
             iter++) {
            keys.insert(iter->name);
        }
        for (auto group : groups->children_) {
            switch (group->expr_type_) {
                case node::kExprColumnRef: {
                    std::string column =
                        dynamic_cast<node::ColumnRefNode*>(group)
                            ->GetColumnName();
                    // skip group when match index keys
                    if (keys.find(column) == keys.cend()) {
                        new_groups->AddChild(group);
                    }
                    break;
                }
                default: {
                    new_groups->AddChild(group);
                }
            }
        }
        *output = new_groups;
        return true;
    } else {
        return false;
    }
}
bool GroupByOptimized::MatchBestIndex(std::vector<std::string>& columns,
                                      const IndexHint& index_hint,
                                      std::vector<bool>& bitmap,
                                      std::string& index_name) {
    std::set<std::string> column_set;
    for (int i = 0; i < columns.size(); ++i) {
        if (bitmap[i]) {
            column_set.insert(columns[i]);
        }
    }

    for (auto iter = index_hint.cbegin(); iter != index_hint.cend(); iter++) {
        IndexSt index = iter->second;
        std::set<std::string> keys;
        for (auto key_iter = index.keys.cbegin(); key_iter != index.keys.cend();
             key_iter++) {
            keys.insert(key_iter->name);
        }

        if (column_set == keys) {
            index_name = index.name;
            return true;
        }
    }

    bool succ = false;
    for (int i = 0; i < bitmap.size(); ++i) {
        if (bitmap[i]) {
            bitmap[i] = false;
            std::string name;
            if (MatchBestIndex(columns, index_hint, bitmap, name)) {
                succ = true;
                if (index_name.empty()) {
                    index_name = name;
                } else {
                    auto org_index = index_hint.at(index_name);
                    auto new_index = index_hint.at(name);
                    if (org_index.keys.size() < new_index.keys.size()) {
                        index_name = name;
                    }
                }
            }
            bitmap[i] = true;
        }
    }
    return succ;
}

// This is primarily intended to be used on top-level WHERE (or JOIN/ON)
// clauses.  It can also be used on top-level CHECK constraints, for which
// pass is_check = true.  DO NOT call it on any expression that is not known
// to be one or the other, as it might apply inappropriate simplifications.
bool CanonicalizeExprTransformPass::Transform(node::ExprNode* in,
                                              node::ExprNode** output) {
    // 1. 忽略NULL以及OR中的False/AND中的TRUE
    // 2. 拉平谓词
    // 3. 清除重复ORs
    // 4.
    return false;
}
bool TransformUpPysicalPass::Apply(PhysicalOpNode* in, PhysicalOpNode** out) {
    auto producer = in->GetProducers();
    for (int j = 0; j < producer.size(); ++j) {
        PhysicalOpNode* output;
        if (Apply(producer[j], &output)) {
            in->UpdateProducer(j, output);
        }
    }
    return Transform(in, out);
}
}  // namespace vm
}  // namespace fesql
