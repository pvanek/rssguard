#include "gui/formcategorydetails.h"

#include "core/defs.h"
#include "core/feedsmodelrootitem.h"
#include "core/feedsmodelcategory.h"
#include "core/feedsmodel.h"
#include "gui/iconthemefactory.h"
#include "gui/feedsview.h"

#include <QPushButton>


FormCategoryDetails::FormCategoryDetails(FeedsModel *model, QWidget *parent)
  : QDialog(parent), m_editableCategory(NULL) {
  initialize();
  loadCategories(model->getAllCategories().values(),
                 model->rootItem());

  setWindowTitle(tr("Add new category"));
}

FormCategoryDetails::~FormCategoryDetails() {
  qDebug("Destroying FormCategoryDetails instance.");
}

void FormCategoryDetails::setEditableCategory(FeedsModelCategory *editable_category) {
  m_editableCategory = editable_category;

  if (m_editableCategory != NULL) {
    // TODO: Setup the dialog according to new category.
    // so remove this category from category combobox!!
  }
}

int FormCategoryDetails::exec(FeedsModelCategory *input_category,
                              FeedsModelCategory *output_item,
                              FeedsModelRootItem *parent_item) {
  // TODO: Implement this.
  setEditableCategory(input_category);

  int result = QDialog::exec();

  if (input_category == NULL) {
    // User is adding new category.
  }
  else {
    // User is editing existing category.
  }

  return result;
}

void FormCategoryDetails::initialize() {
  m_ui = new Ui::FormCategoryDetails();
  m_ui->setupUi(this);

  // Set flags and attributes.
  setWindowFlags(Qt::MSWindowsFixedSizeDialogHint | Qt::Dialog);
  setWindowIcon(IconThemeFactory::getInstance()->fromTheme("document-new"));
}

void FormCategoryDetails::loadCategories(const QList<FeedsModelCategory *> categories,
                                         FeedsModelRootItem *root_item) {
  m_ui->m_cmbParentCategory->addItem(root_item->icon(),
                                     root_item->title(),
                                     root_item->id());

  foreach (FeedsModelCategory *category, categories) {
    m_ui->m_cmbParentCategory->addItem(category->data(FDS_MODEL_TITLE_INDEX,
                                                      Qt::DecorationRole).value<QIcon>(),
                                       category->title(),
                                       category->id());
  }
}
